import cv2
import os
import torch
from torch.utils.data import DataLoader
from torch.utils.data import Dataset as BaseDataset

os.environ["NO_ALBUMENTATIONS_UPDATE"] = "1"
import albumentations as albu
import sys

import segmentation_models_pytorch as smp
import segmentation_models_pytorch.utils

import numpy as np
import pandas as pd 
from PIL import Image
import shutil
import glob

import random
from tqdm.auto import tqdm

import definition



if __name__ == "__main__":
    # sys.argv[1:]로 스크립트명 이후의 인자만 추출
    image_paths = sys.argv[1:]  # 이미지 폴더 경로
    image_file = [x.split("\\")[-1] for x in image_paths]
    image_dir = image_paths[0].split("\\")[:-1]  # 첫 번째 경로 사용

    current_path = os.path.dirname(os.path.abspath(__file__)) # main 폴더의 절대 경로
    workspace_path = os.path.join(current_path, "model_train","model_load")
    dataset_name = 'Full_Image'
    dataset_path=os.path.join(workspace_path, dataset_name)
    ckpt_path = os.path.join(dataset_path,'Autolabeling_dataset','ckpt')
    
    sample_path = os.path.join(current_path,"result2")
    sample_crop = os.path.join(sample_path,'preprocessing','Crop')  # 자른 이미지
    sample_label = os.path.join(sample_path,'preprocessing','Label')  # 자른 라벨

    sample_prediction = os.path.join(sample_path,'prediction')  # 마스크 결과
    sample_imgs = os.path.join(sample_path,'imgs')  # 원본 이미지

    
    # pd.set_option('display.max_colwidth', 5500)

    # base_name = ['.'.join(x.split('.')[:-1]) for x in image_file]
    # df = pd.DataFrame({
    #     'base_names': base_name,
    #     'file_name': image_file,
    #     'file_dir': [os.path.join(image_dir, x) for x in image_file],
    #     'autolabeling_dir': [os.path.join(sample_label, f'{x}_crop') for x in base_name],  # 타일 라벨링 결과
    #     'img_dir': [os.path.join(sample_imgs, f'{x}_compare') for x in base_name]})  # 라벨링 비교 이미지 결과
    
    # definition.rebuild_dir(sample_crop)
    # definition.cropping_image(df, sample_crop)  # 이미지 자르기: 512*512

    # ENCODER = 'efficientnet-b2'
    # ENCODER_WEIGHTS = 'imagenet'
    # CLASSES = ['cell']
    # ACTIVATION = 'sigmoid' # could be None for logits or 'softmax2d' for multiclass segmentation
    # DEVICE = 'cuda' if torch.cuda.is_available() else 'cpu'
    # # create segmentation model with pretrained encoder
    # model = smp.UnetPlusPlus(
    #     encoder_name=ENCODER,
    #     encoder_weights=ENCODER_WEIGHTS,
    #     classes=len(CLASSES),
    #     activation=ACTIVATION,
    # )
    # preprocessing_fn = smp.encoders.get_preprocessing_fn(ENCODER, ENCODER_WEIGHTS)
    # # Create test dataset
    # test_dataset = definition.InferenceDataset(
    #     images_dir=image_dir,
    #     classes=CLASSES,
    #     preprocessing= definition.get_preprocessing(preprocessing_fn),
    # )
    
    # best_model = torch.load(os.path.join(ckpt_path,'{}_model2.pth'.format(dataset_name)), map_location=DEVICE)

    # test_dataloader = DataLoader(
    #     test_dataset, 
    #     batch_size=8, # 한 번에 하나의 원본 이미지(의 타일들)를 처리
    #     shuffle=False, 
    #     num_workers=0,  # num_workers/는 시스템의 CPU 코어 수를 활용할 수 있지만, 여기서는 0으로 설정하여 단일 프로세스에서 실행
    #     # num_workers=os.cpu_count(), # 시스템의 CPU 코어 수 활용
    #     collate_fn=definition.inference_collate_fn
    # )
    
    # definition.build_dir(sample_label)
    # definition.build_dir(sample_prediction)

    # @torch.no_grad() # 추론 시에는 그래디언트 계산을 비활성화하여 메모리 사용량을 줄이고 속도를 높입니다.
    # def inference_on_folder(model, dataloader, device, output_dir, inference_dir):
    #     model.eval().to(device)
    #     progress_bar = tqdm(dataloader, desc="Inference Progress")
        
    #     full_masks = {}
    #     num = 0
        
    #     for i, data in enumerate(progress_bar):
    #         tiles_batch, h_starts, w_starts, heights, widths, image_names = data
    #         #print(f"\n[{i+1}/{len(dataloader)}] 배치 추론 중...")        
    #         tiles_batch = (tiles_batch).to(device)  
            
    #         # 모델 예측
    #         prediction = model(tiles_batch) # 출력: (batch_size, num_classes, H, W)
    #         predictions = prediction.cpu().numpy() 
            
    #         for j in range(len(image_names)):
    #             img_name = image_names[j]
    #             h_start = h_starts[j]
    #             w_start = w_starts[j]
    #             height = heights[j]
    #             width = widths[j]
    #             predicted_mask_tile = predictions[j]
    #             # 활성화 함수 적용 후 CPU로 이동하고 NumPy 배열로 변환
    #             # sigmoid 활성화 함수를 사용하면 [0, 1] 범위의 확률 맵이 출력됩니다.
    #             # 'cell' 클래스 하나만 예측하므로 prediction.squeeze()를 사용합니다.
    #             # threshold를 적용하여 이진 마스크로 변환 (0.5 이상이면 1, 아니면 0)
                
    #             # 단일 클래스 이진 세그멘테이션의 경우 (1, H, W) -> (H, W)
    #             if len(CLASSES) == 1:
    #                 predicted_mask_tile = predicted_mask_tile.squeeze(0) # (H, W)
    #                 predicted_mask_tile = (predicted_mask_tile > 0.5).astype(np.uint8) * 255 # 이진 마스크로 변환 (0 또는 255)
    #             else:
    #                 # 다중 클래스 세그멘테이션의 경우 argmax 사용
    #                 predicted_mask_tile = np.argmax(predicted_mask_tile, axis=0).astype(np.uint8) # (H, W)
    #                 # 클래스 ID를 실제 픽셀 값으로 매핑해야 할 수 있습니다. (예: 0, 1, 2... 에 따라 다른 색상)
    #                 # 여기서는 예시로 255로 스케일링하여 저장합니다.
    #                 predicted_mask_tile = predicted_mask_tile * (255 // (len(CLASSES) -1)) if len(CLASSES) > 1 else predicted_mask_tile * 255
    #             tile_path = os.path.join(inference_dir,f"{img_name}_crop", f"{num:05d}.png")
    #             cv2.imwrite(tile_path, predicted_mask_tile)     
                
    #             if definition.has_less_white_pixels(tile_path, 1000):
    #                 os.remove(tile_path)
    #                 continue
    #             num += 1

    #             # --- 전체 마스크 재구성 로직 ---
    #             # 현재 이미지에 대한 전체 마스크가 딕셔너리에 없으면 초기화
    #             if img_name not in full_masks:
    #                 full_masks[img_name] = np.zeros((height, width), dtype=np.uint8)
                
    #             h_end = min(h_start + 512, height)
    #             w_end = min(w_start + 512, width)
                
    #             # 재구성할 마스크의 실제 높이와 너비
    #             actual_reconstruct_height = h_end - h_start
    #             actual_reconstruct_width = w_end - w_start
                
    #             # 예측된 타일 마스크에서 원본 영역에 해당하는 부분만 추출
    #             reconstructed_tile = predicted_mask_tile[:actual_reconstruct_height, :actual_reconstruct_width]
                
    #             # 전체 마스크에 재구성
    #             full_masks[img_name][h_start:h_end, w_start:w_end] = reconstructed_tile
    #         # 현재 배치의 추론 진행 상황 출력
    #         #print(f"[{i+1}/{len(dataloader)}] 배치 추론 완료.")

    #     # 모든 배치가 처리된 후, 최종 마스크들을 파일로 저장
    #     print("\n모든 이미지 타일 추론이 완료되었습니다. 최종 결과를 저장합니다. 💾")
        
    #     for img_name, full_mask in full_masks.items():
    #         output_path = os.path.join(output_dir, f"{img_name}_mask.png")
    #         cv2.imwrite(output_path, full_mask)
    #         print(f"'{img_name}'의 예측 마스크를 '{output_path}'에 저장했습니다. ✅")

    # # 추론 실행
    # inference_on_folder(best_model, test_dataloader, DEVICE, sample_prediction, sample_label)
    

    # definition.build_dir(sample_prediction)
    count = len(os.listdir(sample_prediction))

    print(f"{count}/{sample_prediction}")
