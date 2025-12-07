# -*- coding: utf-8 -*-
import os
import sys

# [중요] Albumentations 업데이트 체크 비활성화 (import보다 먼저 해야 함)
os.environ["NO_ALBUMENTATIONS_UPDATE"] = "1"

from fastapi import FastAPI, UploadFile, File
from contextlib import asynccontextmanager
import torch
import numpy as np
import cv2
import io
import albumentations as albu
import warnings
from starlette.responses import StreamingResponse

# 경고 메시지 무시
warnings.filterwarnings("ignore")

# ==========================================
# 1. 전역 변수 및 설정
# ==========================================
model_context = {}
DEVICE = 'cuda' if torch.cuda.is_available() else 'cpu'
# 도커 내 경로에 맞게 설정 (같은 폴더에 ckpt가 있다고 가정)
CKPT_PATH = './ckpt/Full_Image_weights_jit.pt'

# ==========================================
# 2. 전처리 함수 정의
# ==========================================
def to_tensor(x, **kwargs):
    """이미지 배열을 PyTorch Tensor로 변환 (HWC -> CHW)"""
    if x.ndim == 2: 
        x = np.expand_dims(x, axis=-1)
    if x.shape[2] == 1: 
        x = np.repeat(x, 3, axis=2)
    x = x.transpose(2, 0, 1).astype('float32')
    return torch.from_numpy(x)

def get_preprocessing_transform():
    """이미지 정규화 및 텐서 변환"""
    # EfficientNet-B2 학습 시 사용된 평균/표준편차 적용
    return albu.Compose([
        albu.Normalize(mean=(0.485, 0.456, 0.406), std=(0.229, 0.224, 0.225)),
        albu.Lambda(image=to_tensor),
    ])

# 전처리 객체 미리 생성
preprocess_fn = get_preprocessing_transform()

# ==========================================
# 3. 서버 수명 주기 (Lifespan): 모델 로드
# ==========================================
@asynccontextmanager
async def lifespan(app: FastAPI):
    print(f"[Server] 시스템 시작... 디바이스: {DEVICE}", flush=True)
    print(f"[Server] 모델 파일 경로 확인: {os.path.abspath(CKPT_PATH)}", flush=True)
    print("[Server] AI 모델 로딩 중... (JIT)", flush=True)
    
    if not os.path.exists(CKPT_PATH):
        print(f"[Error] 모델 파일이 없습니다! 경로를 확인하세요: {CKPT_PATH}", flush=True)
        # 파일이 없으면 빈 딕셔너리로 둠 (요청 시 에러 처리됨)
    else:
        try:
            # JIT 모델 로드 (구조 정의 필요 없음)
            model = torch.jit.load(CKPT_PATH, map_location=DEVICE)
            model.eval()
            
            # 워밍업 (선택 사항: 더미 데이터로 한 번 돌려서 캐싱)
            # print("  -> 워밍업 실행 중...", flush=True)
            # dummy = torch.zeros(1, 3, 512, 512).to(DEVICE)
            # with torch.no_grad():
            #     model(dummy)
                
            model_context['model'] = model
            print("[Server] 모델 로드 완료! 추론 준비 끝.", flush=True)
            
        except Exception as e:
            print(f"[Server] 모델 로드 중 치명적 오류 발생: {e}", flush=True)
            import traceback
            traceback.print_exc()
        
    yield  # 서버 실행 (여기서 멈춰 있다가 종료 시 아래 코드 실행)
    
    # 서버 종료 시 정리
    model_context.clear()
    print("[Server] 시스템 종료.", flush=True)

# FastAPI 앱 생성 (lifespan 등록)
app = FastAPI(lifespan=lifespan)

# ==========================================
# 4. 추론 API 엔드포인트
# ==========================================
@app.post("/predict")
async def predict(file: UploadFile = File(...)):
    # 모델 로드 확인
    if 'model' not in model_context:
        return {"error": "Model is not loaded. Check server logs."}

    try:
        # 1. 이미지 수신 및 디코딩
        contents = await file.read()
        nparr = np.frombuffer(contents, np.uint8)
        img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
        
        if img is None:
            return {"error": "Invalid image data"}

        # OpenCV는 BGR이므로 RGB로 변환 (모델 학습 환경에 맞춤)
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)

        # 2. 전처리 (Resize가 필요하다면 여기서 추가해야 함. 현재는 512 crop 전제)
        # sample = preprocess_fn(image=img) ["image"]
        # 위 방식 혹은 직접 호출:
        transformed = preprocess_fn(image=img)
        img_tensor = transformed['image'].to(DEVICE)

        # 3. 추론 (Inference)
        with torch.no_grad():
            output = model_context['model'](img_tensor)
            
            # 4. 후처리 (Post-processing)
            # JIT 모델 안에 Sigmoid가 포함되어 있는지 확인 필요.
            # 보통 학습 시 포함 안 하므로 여기서 적용 (안전을 위해)
            # output = torch.sigmoid(output) 
            mask = output.squeeze().cpu().numpy()
            mask = mask.squeeze(0)
            # Thresholding (0.5 기준 이진화)
            mask = (mask > 0.5).astype(np.uint8) * 255

        # 5. 결과 반환 (이미지를 PNG 바이트로 변환)
        res, im_png = cv2.imencode(".png", mask)
        
        # StreamingResponse로 이미지 전송
        return StreamingResponse(io.BytesIO(im_png.tobytes()), media_type="image/png")

    except Exception as e:
        print(f"[Error] 추론 중 오류 발생: {e}", flush=True)
        return {"error": str(e)}

# ==========================================
# 5. 실행 진입점 (python server.py 로 실행 시)
# ==========================================
if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8888)

# 끌때
# netstat -ano | findstr :8888
# taskkill /PID 12345 /F


