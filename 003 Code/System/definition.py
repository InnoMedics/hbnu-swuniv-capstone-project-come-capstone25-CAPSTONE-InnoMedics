# -*- coding: utf-8 -*-
from torch.utils.data import Dataset as BaseDataset
import torch
import albumentations as albu

import segmentation_models_pytorch as smp

import numpy as np
import pandas as pd 

import shutil

import os
import random
from tqdm.auto import tqdm
import cv2
import matplotlib.pyplot as plt

def seed_everything(seed):
    random.seed(seed)
    os.environ['PYTHONHASHSEED'] = str(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed(seed)
    torch.backends.cudnn.deterministic = True

def build_dir(target_path):
    if not os.path.exists(target_path):
        os.makedirs(target_path)
def rebuild_dir(target_path):
    """폴더가 존재하면 삭제하고 새로 생성합니다."""
    if os.path.exists(target_path):
        shutil.rmtree(target_path)
    os.makedirs(target_path)
    

def cropping_image(df, save_path):
    size = 512
    for idx in tqdm(df.index):
        base_names = df.loc[idx,'base_names']    
        img_path = df.loc[idx,'file_dir']

        img_rgb=cv2.imread(img_path, cv2.IMREAD_COLOR)  # 원본이미지
        max_height,max_width = img_rgb.shape[0],img_rgb.shape[1]

        num=0
        label_dir = df.loc[idx,'autolabeling_dir']
        if not os.path.exists(label_dir):
            os.makedirs(label_dir)
            
        for height in tqdm(range(0, img_rgb.shape[0], size)):
            for width in range(0, img_rgb.shape[1], size):

                img_rgb_crop = img_rgb[height:height+size, width:width+size, :]  # 30*30 절삭(높이, 너비)
                if height+size > max_height or width+size > max_width:
                    pass
                else:
                    name = str(base_names)+'_TIFF_Cropped_'+'%05d.png' % num
                    tile_dir = os.path.join(save_path, name)
                    cv2.imwrite(tile_dir,img_rgb_crop)
                    num += 1
            
def has_less_white_pixels(image_path, threshold=1):


    """
    이미지에서 흰색 픽셀(255)이 threshold 미만인지 확인하는 함수
    """
    image = cv2.imread(image_path, cv2.IMREAD_GRAYSCALE)
    white_pixel_count = np.sum(image == 255)
    return white_pixel_count < threshold

class InferenceDataset(BaseDataset):
    
    def __init__(
        self,
        images_dir,
        classes=None,
        preprocessing=None,
    ):
        self.images_fps = [os.path.join(images_dir, image_id) for image_id in os.listdir(images_dir)]  # image_path
        self.preprocessing = preprocessing
        self.tile_size = 512

        # 전체 이미지에서 모든 타일 정보를 미리 생성
        self.tiles_info = []
        for img_idx, image_path in enumerate(self.images_fps):
            image = cv2.imread(image_path)
            height, width, _ = image.shape
            
            for h in range(0, height, self.tile_size):
                for w in range(0, width, self.tile_size):
                    self.tiles_info.append({
                        'img_idx': img_idx, # 원본 이미지 인덱스
                        'h_start': h,
                        'w_start': w,
                    })

    def __getitem__(self, i):
        # i번째 타일 정보 가져오기
        tile_info = self.tiles_info[i]
        img_idx = tile_info['img_idx']
        h_start = tile_info['h_start']
        w_start = tile_info['w_start']
        
        # 원본 이미지 로드
        image_path = self.images_fps[img_idx]
        image = cv2.imread(image_path)
        # 흑백일 때(2D array) BGR로 바꿔 주기
        if image.ndim == 2:
            image = cv2.cvtColor(image, cv2.COLOR_GRAY2BGR)
        else:
            # 컬러 파일이라면 기본 BGR 로드 → RGB 로 변환
            image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        
        
        # 타일 크롭
        tile = image[h_start:h_start+self.tile_size, w_start:w_start+self.tile_size]
        
        # 전처리 적용
        if self.preprocessing:
            sample = self.preprocessing(image=tile)
            tile = sample['image']

        # 원본 이미지의 이름과 크기 정보 반환
        original_image_name = os.path.splitext(os.path.basename(image_path))[0]    
        original_height, original_width, _ = image.shape
        
        return tile, h_start, w_start, original_height, original_width, original_image_name

    def __len__(self):
        return len(self.tiles_info)   

def to_tensor(x, **kwargs):
    if x.ndim == 2:  # 채널이 없는 경우    
        x = np.expand_dims(x, axis=-1)  # (H, W) → (H, W, 1)
    if x.shape[2] == 1:  # 1채널일 경우 → 3채널로 반복
        x = np.repeat(x, 3, axis=2)
    x = x.transpose(2, 0, 1).astype('float32')  # (C, H, W)
    return torch.from_numpy(x)
def get_preprocessing(preprocessing_fn):
    _transform = [
        albu.PadIfNeeded(512, 512, always_apply=True, border_mode=cv2.BORDER_CONSTANT, value=0),
        albu.Lambda(image=preprocessing_fn),
        albu.Lambda(image=to_tensor),
    ]
    return albu.Compose(_transform)
def inference_collate_fn(batch):
    """
    batch: [(tile, h, w, H, W, name),  … ]  형태의 리스트
    -> ([tiles…], [h_starts…], [w_starts…], [heights…], [widths…], [names…])
    """
    tiles, h_starts, w_starts, heights, widths, names = zip(*batch)
    tiles_batch = torch.stack(tiles)
    # tiles: tuple of tensors, h_starts 등은 tuple of int, names는 tuple of str
    return (tiles_batch), list(h_starts), list(w_starts), list(heights), list(widths), list(names)
    
def draw_contours_on_image(image, mask, color):
    """Mask의 테두리를 주어진 색상으로 이미지에 그립니다."""
    contours, _ = cv2.findContours(mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
    image_with_contours = image.copy()
    cv2.drawContours(image_with_contours, contours, -1, color, 2)
    return image_with_contours
def count_pixels_in_mask(mask):
    """마스크 안의 픽셀 수를 반환합니다."""
    return np.sum(mask)
