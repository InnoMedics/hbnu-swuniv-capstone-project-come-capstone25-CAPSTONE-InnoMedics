#pragma once
#include "pch.h"  
#include <vector>
#include <afxwin.h>
#include <afxcmn.h>

#include <wrl.h>
#include <wincodec.h>
#include <d2d1.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d2d1.lib")

using Microsoft::WRL::ComPtr;

class ImageDialog : public CDialogEx
{
    DECLARE_DYNAMIC(ImageDialog);

public:
    ImageDialog(CString folderPath, CWnd* pParent = nullptr);
    virtual ~ImageDialog();

    enum { IDD = IDD_IMAGE_DIALOG };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void PostNcDestroy() override;

    DECLARE_MESSAGE_MAP()

private:
    HICON m_hIcon;
    CString m_folderPath;                       // prediction 폴더 경로
    std::vector<CString> m_resultFolders;       // 1_result, 2_result, ...
    std::vector<HBITMAP> m_hBitmaps;            // WIC로 로딩한 비트맵 4개
    int m_currentResultIndex;                   // 현재 표시 인덱스
    // [추가] 현재 확대된 이미지 인덱스 (-1: 없음/그리드뷰, 0~3: 확대됨)
    int m_nZoomedIndex;
    // [추가] 클릭 처리 헬퍼 함수
    void ToggleZoom(int index);

    CStatic m_imgCtrl[4];                       // 이미지 출력용 4개
    CStatic m_staticName;
    CButton m_btnPrev;
    CButton m_btnNext;

    ComPtr<IWICImagingFactory> m_wicFactory;    // WIC Factory

    void LoadResultFolders();
    void ShowResultFolder(int index);
    void RepositionImageControls(int cx, int cy);

    HBITMAP LoadBitmapWIC(const CString& path);        // 핵심: 파일 → HBITMAP
    HBITMAP CreateHBITMAPFromWIC(IWICBitmapSource* wbSource);

public:
    afx_msg void OnPaint();
    afx_msg void OnBnClickedButtonPrev();
    afx_msg void OnBnClickedButtonNext();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    
    // [추가] 4개 이미지에 대한 클릭 이벤트 핸들러
    afx_msg void OnStnClickedStaticImg1();
    afx_msg void OnStnClickedStaticImg2();
    afx_msg void OnStnClickedStaticImg3();
    afx_msg void OnStnClickedStaticImg4();
};

