// MFCApplication2Dlg.h: 헤더 파일
//


#pragma once

#include "afxwin.h" // MFC 기본 헤더 포함
#include <vector>
#include <string>

// [1] 사용자 정의 메시지 (스레드 -> 메인화면 통신용)
#define WM_THREAD_UPDATE_MSG  (WM_USER + 100) // 진행 상황 텍스트 전송
#define WM_THREAD_FINISHED    (WM_USER + 101) // 작업 완료 신호

// [2] 스레드에 넘겨줄 데이터 구조체 (가방)
struct ThreadParam {
    HWND hWnd = NULL;                 // 메인 윈도우 핸들 (쪽지 보낼 주소)
    std::vector<std::string> imagePaths; // 파이썬에 보낼 경로들
};

// CMFCApplication2Dlg 대화 상자
class CMFCApplication2Dlg : public CDialogEx
{
public:
    // 생성자
    CMFCApplication2Dlg(CWnd* pParent = nullptr);
    
    // [3] 메시지 처리 함수 선언 (afx_msg 필수)
    afx_msg LRESULT OnThreadUpdate(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnThreadFinished(WPARAM wParam, LPARAM lParam);

    // 스레드 함수는 static이어야 합니다.
    static UINT PythonThreadProc(LPVOID pParam);
    // 프로그래스 바 제어 변수
    //CProgressCtrl m_progress;
    // 실행 중인 파이썬 프로세스 핸들 저장용
    HANDLE m_hRunningProcess;
    HANDLE m_hJob;
    // 여기까지
    
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_MFCAPPLICATION2_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX); // DDX/DDV 지원


protected:
    HICON m_hIcon;

    // 메시지 맵 함수
    virtual BOOL OnInitDialog();
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    DECLARE_MESSAGE_MAP()

private:
    CStringArray selectedFilePaths; // 여러 개의 파일 경로 저장


public:
    afx_msg void OnBnClickedButtonSelect();  // 파일 선택 버튼
    afx_msg void OnBnClickedButtonAnalyze(); // 분석 버튼
    afx_msg void OnBnClickedButtonReset();   // 초기화 버튼
    afx_msg void OnEnChangeStaticPath();
    afx_msg void OnBnClickedButtonCheck();
    afx_msg void OnStnClickedStaticResult();
    afx_msg void OnDestroy();
};