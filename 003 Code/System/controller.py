# -*- coding: utf-8 -*-
import sys
import os
import warnings
import requests
import numpy as np
import pandas as pd
import cv2
from tqdm import tqdm
import time
import definition # 공통 모듈

# [MFC 통신 설정]
sys.stdout = definition.GhostWriter(sys.stdout)
# 모든 경고 메시지 무시 (깔끔한 출력을 위해)
warnings.filterwarnings("ignore")
# [2] stderr(에러 출력)은 파일로 돌려버림 (화면에 안 뜨게)
# 실행 파일 위치에 error_log.txt 생성
if getattr(sys, 'frozen', False):
    base_dir = os.path.dirname(sys.executable)
    
else:
    base_dir = os.path.dirname(os.path.abspath(__file__))

log_path = os.path.join(base_dir, "error_log.txt")

# 에러가 나면 이 파일을 열어서 기록함 (화면엔 안 보임)
sys.stderr = open(log_path, "w", encoding="utf-8")

# 서버 설정
SERVER_URL = "http://localhost:8888/predict"

def main():
    definition.seed_everything(42)
    print("1/5: 클라이언트 시작 & 경로 설정", flush=True)

    if len(sys.argv) < 2:
        print("Error: 이미지 경로가 없습니다.", flush=True)
        sys.exit(1)

    image_paths = sys.argv[1:]
    
    # 경로 계산
    # first_img_dir = os.path.dirname(image_paths[0])
    image_file_names = [os.path.basename(p) for p in image_paths]
    base_names = [os.path.splitext(f)[0] for f in image_file_names]

    # 실행 파일 위치 기준 결과 폴더 잡기 (definition 함수 쓰면 더 좋음)
    if getattr(sys, 'frozen', False):
        base_dir = os.path.dirname(sys.executable)
    else:
        base_dir = os.path.dirname(os.path.abspath(__file__))
    
    root_dir = os.path.abspath(os.path.join(base_dir, "..",".."))
    results_path = os.path.join(root_dir, "results")
    # results_path = os.path.join(base_dir, "results")
    
    sample_crop = os.path.join(results_path, 'preprocessing', 'Crop')
    sample_label = os.path.join(results_path, 'preprocessing', 'Label')
    sample_prediction = os.path.join(results_path, 'prediction')
    sample_segmentation = os.path.join(results_path, 'segmentation')

    df = pd.DataFrame({
        'base_names': base_names,
        'file_name': image_file_names,
        'file_dir': image_paths,
        'autolabeling_dir': [os.path.join(sample_label, f'{x}_crop') for x in base_names],
        'img_dir': [os.path.join(sample_prediction, f'{x}_result') for x in base_names]
    })

    # 폴더 초기화
    definition.rebuild_dir(sample_crop)
    definition.build_dir(sample_label)
    definition.build_dir(sample_prediction)
    definition.build_dir(sample_segmentation)

    # 1. 이미지 자르기 (로컬 수행)
    print("2/5: 이미지 전처리 (Tiling)", flush=True)
    definition.cropping_image(df, sample_crop)

    # 2. 서버 통신 및 추론
    print("3/5: 서버에 추론 요청 중", flush=True)
    
    # 잘린 타일(Crop) 이미지들을 모두 찾음
    all_tiles = []
    for idx, row in df.iterrows():
        # crop_dir = os.path.join(sample_crop, f"{row['base_names']}_Cropped_*.png")
        # glob 등을 써도 되지만, cropping_image 규칙대로 파일명 추측
        # 여기서는 간단히 os.listdir로 해당 폴더 검색
        # 주의: cropping_image는 sample_crop/{파일명} 형태가 아니라 
        #      sample_crop/{파일명_Cropped_00000.png} 형태로 바로 저장함.
        #      definition.cropping_image 함수 로직 확인 필요.
        #      (위 definition 코드는 save_path 아래에 바로 저장함)
        
        # 그냥 sample_crop 폴더를 뒤져서 해당 이미지의 타일만 골라냄
        tiles = [os.path.join(sample_crop, f) for f in os.listdir(sample_crop) 
                if f.startswith(f"{row['base_names']}_Cropped_") and f.endswith(".png")]
        
        for t in tiles:
            all_tiles.append({'tile_path': t, 'base_name': row['base_names']})

    # MFC 프로그래스 바 연동
    
    # 결과를 담을 임시 저장소 (복원용)
    # { '이미지이름': np.zeros((H,W)) } 형태가 필요하지만
    # 간단하게: 받은 마스크 타일을 로컬에 저장 -> 나중에 합치기?
    # 혹은 여기서 바로 저장. (여기서는 바로 저장 후 나중에 합치는 로직이 복잡하므로)
    # 기존 analyze.py 로직처럼 메모리에서 합치려면 원본 크기를 알아야 함.
    
    # [전략 수정] 타일 단위로 서버에 보내고, 받은 마스크를 즉시 로컬에 저장
    # 재구성은 나중에 함.
    progress_bar = tqdm(all_tiles, disable=True) 
    
    success_cnt = 0
    total_count = len(all_tiles)

    # [1] 시작 시간 기록
    start_time = time.time()

    for i, item in enumerate(progress_bar):
        tile_path = item['tile_path']
        base_name = item['base_name']
        
        # -----------------------------------------------------------
        # [수정] 남은 시간(ETA) 직접 계산 로직 (tqdm 의존 X)
        # -----------------------------------------------------------
        time_str = "계산 중..."
        
        if i > 0: # 첫 번째 아이템 이후부터 계산 가능
            current_time = time.time()
            elapsed_time = current_time - start_time
            
            # 처리한 개수 / 걸린 시간 = 초당 처리 속도
            processed_count = i + 1
            if elapsed_time > 0:
                # 1개 처리하는 데 걸리는 평균 시간
                avg_time_per_item = elapsed_time / processed_count
                
                # 남은 개수 * 개당 평균 시간 = 남은 시간
                remaining_items = total_count - processed_count
                remaining_seconds = int(remaining_items * avg_time_per_item)
                
                # 시:분:초 변환
                m, s = divmod(remaining_seconds, 60)
                h, m = divmod(m, 60)
                time_str = f"{h:02d}:{m:02d}:{s:02d}"
        # -----------------------------------------------------------

        # MFC 화면 출력
        print(f"[{i+1}/{total_count}] 분석 중... {base_name} [남은 시간: {time_str}]", flush=True)

        try:
            # 서버 전송
            with open(tile_path, 'rb') as f:
                files = {'file': (os.path.basename(tile_path), f, 'image/png')}
                res = requests.post(SERVER_URL, files=files)
            
            if res.status_code == 200:
                # 결과 받기 (바이너리 -> 이미지)
                nparr = np.frombuffer(res.content, np.uint8)
                mask_tile = cv2.imdecode(nparr, cv2.IMREAD_GRAYSCALE)
                
                # 마스크 타일 저장 (덮어쓰기 or _mask 붙이기)
                # 재구성을 위해 일단 crop 폴더 혹은 별도 폴더에 저장
                # 여기서는 sample_label/{base_name}_crop/ 에 저장 (기존 로직과 맞춤)
                
                label_dir = os.path.join(sample_label, f"{item['base_name']}_crop")
                definition.build_dir(label_dir)
                
                # 파일명 유지 (00000.png)
                tile_name = os.path.basename(tile_path).split('_')[-1] # 00000.png
                save_path = os.path.join(label_dir, tile_name)
                
                definition.imwrite_safe(save_path, mask_tile)
                
                # 빈 이미지 체크
                if definition.has_less_white_pixels(save_path):
                    os.remove(save_path)
                
                success_cnt += 1
                
        except Exception as e:
            print(f"Server Error: {e}", flush=True)
    # 반복문 끝
    print(f"분석 완료! 총 {success_cnt}장 저장됨.", flush=True)
    
    # 마지막 경로 전송 (MFC 팝업용)
    print(f"{success_cnt}/{sample_prediction}", flush=True)

    # 3. 전체 마스크 재구성 (Stitching) 및 시각화
    print("4/5: 결과 병합 및 저장", flush=True)
    
    for idx, row in df.iterrows():
        base_name = row['base_names']
        
        # 원본 읽기 (크기 정보 필요)
        org_img = definition.imread_safe(row['file_dir'])
        if org_img is None: continue
        h, w, _ = org_img.shape
        
        full_mask = np.zeros((h, w), dtype=np.uint8)
        
        # 저장된 마스크 타일들 불러와서 붙이기
        label_dir = os.path.join(sample_label, f"{base_name}_crop")
        if not os.path.exists(label_dir): continue
        
        mask_tiles = sorted(os.listdir(label_dir))
        
        # 타일 인덱스 추적 (단순 순서대로가 아니라, 좌표 계산 필요)
        # cropping_image 함수가 순서대로(행 우선) 저장했다고 가정
        # size=512
        size = 512
        tile_idx = 0
        
        for height in range(0, h, size):
            for width in range(0, w, size):
                if height+size <= h and width+size <= w:
                    # 해당 위치에 타일 파일이 존재하는지 확인 (빈 이미지라 삭제됐을 수도 있음)
                    # 파일명 규칙: {num:05d}.png -> tile_idx를 포맷팅해서 찾기
                    t_name = f"{tile_idx:05d}.png"
                    t_path = os.path.join(label_dir, t_name)
                    
                    if os.path.exists(t_path):
                        tile = definition.imread_safe(t_path, cv2.IMREAD_GRAYSCALE)
                        full_mask[height:height+size, width:width+size] = tile
                    
                    tile_idx += 1

        # 결과 저장
        storage_path = row['img_dir']
        definition.rebuild_dir(storage_path)
        
        # 블러 & 컨투어
        img_blur, full_contour = definition.blurrd_img(row['file_dir'])
        if img_blur is None: img_blur = org_img.copy()
        
        pred_img = definition.draw_contours_on_image(img_blur, full_mask, (0,255,0), full_contour)
        
        print(f"Saved: {base_name}", flush=True)

        definition.imwrite_safe(os.path.join(storage_path, f"{base_name}_original.png"), org_img)
        definition.imwrite_safe(os.path.join(storage_path, f"{base_name}_blurrd.png"), img_blur)
        definition.imwrite_safe(os.path.join(sample_segmentation, f"{base_name}_mask.png"), full_mask)
        definition.imwrite_safe(os.path.join(storage_path, f"{base_name}_contour.png"), pred_img)
        
        # 히스토그램
        _, hist, bins = definition.analyze_segmentation_mask(os.path.join(sample_segmentation, f"{base_name}_mask.png"))
        definition.plot_histogram_skip_empty(hist, bins, storage_path, base_name)
        
        

    print(f"{len(df)}/{sample_prediction}", flush=True)

if __name__ == "__main__":
    main()