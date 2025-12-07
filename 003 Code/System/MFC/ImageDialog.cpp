#include "pch.h"
#include "MFCApplication2.h"
#include "afxdialogex.h"
#include "ImageDialog.h"
#include "MFCApplication2Dlg.h"
#include <algorithm>
#include <string>

#include <wrl.h>
#include <wincodec.h>
#include <d2d1.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d2d1.lib")

using Microsoft::WRL::ComPtr;

IMPLEMENT_DYNAMIC(ImageDialog, CDialogEx);

ImageDialog::ImageDialog(CString folderPath, CWnd* pParent)
	: CDialogEx(IDD_IMAGE_DIALOG, pParent),
	m_folderPath(folderPath),
	m_currentResultIndex(0),
	m_nZoomedIndex(-1) // [추가] 초기값은 확대 없음(-1)
{
}

ImageDialog::~ImageDialog()
{
	for (auto bmp : m_hBitmaps)
		if (bmp) DeleteObject(bmp);

	CoUninitialize();
}

void ImageDialog::PostNcDestroy()
{
	CDialogEx::PostNcDestroy();
}

void ImageDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	// static 컨트롤
	DDX_Control(pDX, IDC_STATIC_IMG1, m_imgCtrl[0]);
	DDX_Control(pDX, IDC_STATIC_IMG2, m_imgCtrl[1]);
	DDX_Control(pDX, IDC_STATIC_IMG3, m_imgCtrl[2]);
	DDX_Control(pDX, IDC_STATIC_IMG4, m_imgCtrl[3]);

	DDX_Control(pDX, IDC_STATIC_NAME, m_staticName);
	DDX_Control(pDX, IDC_BUTTON_PREV, m_btnPrev);
	DDX_Control(pDX, IDC_BUTTON_NEXT, m_btnNext);

}

BEGIN_MESSAGE_MAP(ImageDialog, CDialogEx)
	ON_WM_PAINT()
	ON_BN_CLICKED(IDC_BUTTON_PREV, &ImageDialog::OnBnClickedButtonPrev)
	ON_BN_CLICKED(IDC_BUTTON_NEXT, &ImageDialog::OnBnClickedButtonNext)
	ON_WM_SIZE()
	ON_STN_CLICKED(IDC_STATIC_IMG3, &ImageDialog::OnStnClickedStaticImg3)
	ON_STN_CLICKED(IDC_STATIC_IMG4, &ImageDialog::OnStnClickedStaticImg4)
	// [추가/수정] 4개 이미지 클릭 연결
	ON_STN_CLICKED(IDC_STATIC_IMG1, &ImageDialog::OnStnClickedStaticImg1)
	ON_STN_CLICKED(IDC_STATIC_IMG2, &ImageDialog::OnStnClickedStaticImg2)
	ON_STN_CLICKED(IDC_STATIC_IMG3, &ImageDialog::OnStnClickedStaticImg3)
	ON_STN_CLICKED(IDC_STATIC_IMG4, &ImageDialog::OnStnClickedStaticImg4)
END_MESSAGE_MAP()

BOOL ImageDialog::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// WIC 초기화
	HRESULT hr = CoInitialize(NULL);
	if (FAILED(hr))
	{
		AfxMessageBox(_T("WIC 초기화 실패"));
		return TRUE;
	}
	// [추가] Static 컨트롤이 클릭 이벤트를 받도록 스타일 수정
	for (int i = 0; i < 4; ++i)
	{
		if (m_imgCtrl[i].GetSafeHwnd())
		{
			m_imgCtrl[i].ModifyStyle(0, SS_NOTIFY);
		}
	}
	

	hr = CoCreateInstance(
		CLSID_WICImagingFactory, NULL,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&m_wicFactory)
	);

	if (FAILED(hr))
	{
		AfxMessageBox(_T("WIC Imaging Factory 생성 실패"));
		return TRUE;
	}

	m_hBitmaps.assign(4, nullptr);

	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	LoadResultFolders();

	if (!m_resultFolders.empty())
		ShowResultFolder(0);

	return TRUE;
}

// --------------------------------------------------------
// prediction 폴더 내의 *_result 폴더 탐색
// --------------------------------------------------------
void ImageDialog::LoadResultFolders()
{
	m_resultFolders.clear();

	CString searchPath = m_folderPath + _T("\\*_result");
	CFileFind finder;
	BOOL bWorking = finder.FindFile(searchPath);

	while (bWorking)
	{
		bWorking = finder.FindNextFile();

		if (finder.IsDirectory() && !finder.IsDots())
			m_resultFolders.push_back(finder.GetFilePath());
	}
}

// --------------------------------------------------------
// 해당 result 폴더의 4개 이미지를 로드 및 표시
// --------------------------------------------------------
void ImageDialog::ShowResultFolder(int index)
{
	if (index < 0 || index >= (int)m_resultFolders.size()) return;
	m_currentResultIndex = index;

	CString folder = m_resultFolders[index];
	CString foundPaths[4];

	// 파일 검색
	CFileFind finder;
	CString searchPath = folder + _T("\\*.*");
	BOOL bWorking = finder.FindFile(searchPath);

	while (bWorking)
	{
		bWorking = finder.FindNextFile();
		if (finder.IsDots() || finder.IsDirectory()) continue;

		CString filename = finder.GetFileName().MakeLower();

		if (filename.Find(_T("_original")) != -1)
			foundPaths[0] = finder.GetFilePath();
		else if (filename.Find(_T("_contour")) != -1)
			foundPaths[1] = finder.GetFilePath();
		else if (filename.Find(_T("_histogram")) != -1)
			foundPaths[2] = finder.GetFilePath();
		else if (filename.Find(_T("_blurrd")) != -1)
			foundPaths[3] = finder.GetFilePath();
	}
	finder.Close();

	// 기존 HBITMAP 해제
	for (auto& h : m_hBitmaps)
	{
		if (h) DeleteObject(h);
		h = nullptr;
	}

	// WIC → HBITMAP 로드
	for (int i = 0; i < 4; ++i)
	{
		if (!foundPaths[i].IsEmpty())
			m_hBitmaps[i] = LoadBitmapWIC(foundPaths[i]);
	}

	// 이름 표시
	int pos = folder.ReverseFind('\\');
	CString folderName = (pos != -1) ? folder.Mid(pos + 1) : folder;
	SetDlgItemText(IDC_STATIC_NAME, folderName);

	Invalidate();
}



// --------------------------------------------------------
// PNG → HBITMAP 변환 함수
// --------------------------------------------------------

HBITMAP ImageDialog::LoadBitmapWIC(const CString& path)
{
	if (!m_wicFactory) return nullptr;

	ComPtr<IWICBitmapDecoder> decoder;
	HRESULT hr = m_wicFactory->CreateDecoderFromFilename(
		path.GetString(),          // CString -> LPCWSTR
		nullptr,
		GENERIC_READ,
		WICDecodeMetadataCacheOnDemand,
		&decoder);

	if (FAILED(hr) || !decoder) return nullptr;

	ComPtr<IWICBitmapFrameDecode> frame;
	hr = decoder->GetFrame(0, &frame);
	if (FAILED(hr) || !frame) return nullptr;

	// 포맷 변환: 32bpp PBGRA (Direct2D 호환)
	ComPtr<IWICFormatConverter> converter;
	hr = m_wicFactory->CreateFormatConverter(&converter);
	if (FAILED(hr) || !converter) return nullptr;

	hr = converter->Initialize(
		frame.Get(),
		GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone,
		nullptr,
		0.0,
		WICBitmapPaletteTypeMedianCut);

	if (FAILED(hr)) return nullptr;

	return CreateHBITMAPFromWIC(converter.Get());
}


// --------------------------------------------------------
// prediction 폴더 내의 *_result 폴더 탐색
// --------------------------------------------------------
HBITMAP ImageDialog::CreateHBITMAPFromWIC(IWICBitmapSource* wbSource)
{
	if (!wbSource) return nullptr;

	UINT width = 0, height = 0;
	if (FAILED(wbSource->GetSize(&width, &height))) return nullptr;
	if (width == 0 || height == 0) return nullptr;

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = (LONG)width;
	bmi.bmiHeader.biHeight = -(LONG)height; // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* pvBits = nullptr;
	// 안전하게 화면 DC를 얻어 CreateDIBSection에 넘깁니다.
	HDC hdcScreen = ::GetDC(NULL);
	HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
	::ReleaseDC(NULL, hdcScreen);

	if (!hBitmap || !pvBits) return nullptr;

	// 버퍼에 픽셀 복사
	UINT stride = width * 4;
	HRESULT hr = wbSource->CopyPixels(nullptr, stride, stride * height, reinterpret_cast<BYTE*>(pvBits));
	if (FAILED(hr))
	{
		DeleteObject(hBitmap);
		return nullptr;
	}

	return hBitmap;
}

// --------------------------------------------------------
// 이미지 그리기
// --------------------------------------------------------
void ImageDialog::OnPaint()
{
	CPaintDC dc(this);

	for (int i = 0; i < 4; ++i)
	{
		if (!m_imgCtrl[i].IsWindowVisible()) continue;

		HBITMAP hBmp = m_hBitmaps[i];
		if (!hBmp) continue;

		CDC* pDC = m_imgCtrl[i].GetDC();
		if (!pDC) continue;

		CRect rect;
		m_imgCtrl[i].GetClientRect(&rect);

		CDC memDC;
		memDC.CreateCompatibleDC(pDC);
		HBITMAP oldBmp = (HBITMAP)memDC.SelectObject(hBmp);

		BITMAP bmp{};
		GetObject(hBmp, sizeof(BITMAP), &bmp);

		int imgW = bmp.bmWidth;
		int imgH = bmp.bmHeight;

		double aspectImg = (double)imgW / imgH;
		double aspectCtrl = (double)rect.Width() / rect.Height();

		int drawW, drawH;
		if (aspectImg > aspectCtrl)
		{
			drawW = rect.Width();
			drawH = (int)(drawW / aspectImg);
		}
		else
		{
			drawH = rect.Height();
			drawW = (int)(drawH * aspectImg);
		}

		int x = (rect.Width() - drawW) / 2;
		int y = (rect.Height() - drawH) / 2;

		SetStretchBltMode(pDC->m_hDC, HALFTONE);

		pDC->StretchBlt(
			x, y, drawW, drawH,
			&memDC,
			0, 0, imgW, imgH,
			SRCCOPY);

		memDC.SelectObject(oldBmp);
		m_imgCtrl[i].ReleaseDC(pDC);
	}
}
// 토글 함수
void ImageDialog::ToggleZoom(int index)
{
    if (m_nZoomedIndex == index)
    {
        // 이미 확대된 상태에서 다시 누르면 -> 원래대로(그리드) 복귀
        m_nZoomedIndex = -1;
    }
    else
    {
        // 확대
        m_nZoomedIndex = index;
    }

    // 레이아웃 재배치 -> OnSize 로직 재활용 또는 직접 호출
    CRect rect;
    GetClientRect(&rect);
    RepositionImageControls(rect.Width(), rect.Height());
    
    // 화면 갱신
    Invalidate();
}

void ImageDialog::OnStnClickedStaticImg1() { ToggleZoom(0); }
void ImageDialog::OnStnClickedStaticImg2() { ToggleZoom(1); }
void ImageDialog::OnStnClickedStaticImg3() { ToggleZoom(2); } // 중복되지 않게 하나만!
void ImageDialog::OnStnClickedStaticImg4() { ToggleZoom(3); }
// --------------------------------------------------------
// 이전 / 다음 폴더 이동
// --------------------------------------------------------
void ImageDialog::OnBnClickedButtonPrev()
{
	if (m_currentResultIndex > 0)
		ShowResultFolder(m_currentResultIndex - 1);
}

void ImageDialog::OnBnClickedButtonNext()
{
	if (m_currentResultIndex < (int)m_resultFolders.size() - 1)
		ShowResultFolder(m_currentResultIndex + 1);
}

// --------------------------------------------------------
// 다이얼로그　크기　조정

// --------------------------------------------------------
void ImageDialog::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);

	// 초기화 전 호출 방지
	if (m_imgCtrl[0].GetSafeHwnd() != nullptr)
	{
		RepositionImageControls(cx, cy);
		Invalidate();
	}
}

void ImageDialog::RepositionImageControls(int cx, int cy)
{
	if (cx <= 0 || cy <= 0) return;

	// 상단 이름 + 버튼 영역 고정
	const int topMargin = 10;
	const int nameBarTop = 15;
	const int nameBarHeight = 30;

	// 현재 컨트롤의 원래 크기를 가져와서 그대로 유지
	CRect nameRect, prevRect, nextRect;
	if (m_staticName.GetSafeHwnd())
	{
		m_staticName.GetWindowRect(&nameRect);
		ScreenToClient(&nameRect);
	}
	if (m_btnPrev.GetSafeHwnd())
	{
		m_btnPrev.GetWindowRect(&prevRect);
		ScreenToClient(&prevRect);
	}
	if (m_btnNext.GetSafeHwnd())
	{
		m_btnNext.GetWindowRect(&nextRect);
		ScreenToClient(&nextRect);
	}

	// 상단 고정된 높이 (버튼/텍스트가 포함된 구간)
	int topBarHeight = max(nameRect.bottom, nextRect.bottom) + topMargin;

	// 여백
	int marginX = 10;
	int marginY = 10;
	
	// 이미지 배치 가능한 영역
	int startY = topBarHeight + marginY;
	int usableWidth = max(cx - marginX * 3, 100);
	int usableHeight = max(cy - topBarHeight - marginY * 3, 100);

	if (m_nZoomedIndex != -1) // 확대 모드
	{
		for (int i = 0; i < 4; ++i)
		{
			if (!m_imgCtrl[i].GetSafeHwnd()) continue;

			if (i == m_nZoomedIndex)
			{
				// 선택된 이미지는 전체 영역 사용 & 보이기
				m_imgCtrl[i].MoveWindow(marginX, startY, usableWidth, usableHeight);
				m_imgCtrl[i].ShowWindow(SW_SHOW);
				// 맨 앞으로 가져와서 버튼 등에 가려지지 않게 함
				m_imgCtrl[i].BringWindowToTop();
			}
			else
			{
				// 선택 안 된 이미지는 숨기기
				m_imgCtrl[i].ShowWindow(SW_HIDE);
			}
		}
	}
	else // 일반 그리드 모드 (기존 로직)
	{
		int gridW = (usableWidth - marginX) / 2; // 중간 여백 고려
		int gridH = (usableHeight - marginY) / 2;

		CRect rects[4];
		rects[0] = CRect(marginX, startY, marginX + gridW, startY + gridH);
		rects[1] = CRect(marginX * 2 + gridW, startY, marginX * 2 + gridW * 2, startY + gridH);
		rects[2] = CRect(marginX, startY + marginY + gridH, marginX + gridW, startY + marginY + gridH * 2);
		rects[3] = CRect(marginX * 2 + gridW, startY + marginY + gridH, marginX * 2 + gridW * 2, startY + marginY + gridH * 2);

		for (int i = 0; i < 4; ++i)
		{
			if (m_imgCtrl[i].GetSafeHwnd())
			{
				m_imgCtrl[i].MoveWindow(&rects[i]);
				m_imgCtrl[i].ShowWindow(SW_SHOW); // 모두 보이기
			}
		}
	}

	// 이름 중앙 고정 (크기 유지)
	if (m_staticName.GetSafeHwnd())
	{
		int nameWidth = nameRect.Width();
		int nameHeight = nameRect.Height();
		int nameX = (cx - nameWidth) / 2;
		int nameY = nameBarTop;
		m_staticName.MoveWindow(nameX, nameY, nameWidth, nameHeight, TRUE);

		// --- 버튼들 IDC_STATIC_NAME 오른쪽에 나란히 배치 ---
		int spacing = 5; // 버튼 사이 여백

		if (m_btnPrev.GetSafeHwnd() && m_btnNext.GetSafeHwnd())
		{
			int btnWidthPrev = prevRect.Width();
			int btnHeightPrev = prevRect.Height();
			int btnWidthNext = nextRect.Width();
			int btnHeightNext = nextRect.Height();

			int btnY = nameBarTop;

			int btnPrevX = nameX + nameWidth + spacing;
			int btnNextX = btnPrevX + btnWidthPrev + spacing;

			m_btnPrev.MoveWindow(btnPrevX, btnY, btnWidthPrev, btnHeightPrev, TRUE);
			m_btnNext.MoveWindow(btnNextX, btnY, btnWidthNext, btnHeightNext, TRUE);
		}
	}
}
