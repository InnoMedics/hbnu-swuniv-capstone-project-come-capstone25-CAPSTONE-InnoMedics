// MFCApplication2Dlg.cpp: 구현 파일
//

#include "pch.h"
#include "MFCApplication2.h"
#include "MFCApplication2Dlg.h"
#include "afxdialogex.h"
#include "ImageDialog.h"
#include <atlconv.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstdio>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


void CMFCApplication2Dlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    // [추가] 리소스 ID와 변수 연결
    //DDX_Control(pDX, IDC_PROGRESS1, m_progress);
}

// [스레드 함수] 
UINT CMFCApplication2Dlg::PythonThreadProc(LPVOID pParam)
{
    ThreadParam* pData = (ThreadParam*)pParam;
    CMFCApplication2Dlg* pDlg = (CMFCApplication2Dlg*)CWnd::FromHandle(pData->hWnd);
    std::string finalResult = "";

    
    // 1. 명령어 조합
    TCHAR szPath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, szPath, MAX_PATH);

    // 2. 파일 이름(예: Application.exe)을 제거하고 디렉토리만 남김
    CString strPath(szPath);
    int nPos = strPath.ReverseFind('\\'); // 마지막 '\' 위치 찾기
    if (nPos > 0)
        strPath = strPath.Left(nPos); // '\' 이전까지만 자름 (예: C:\Users\yy\Desktop\test)
    // 상대 경로 연결 (현재 폴더 안에 results/prediction이 있다고 가정)
    CString Path = strPath + _T("\\..\\..\\..\\dist\\analyze\\controller.py");
    //CString Path = strPath + _T("\\..\\..\\..\\dist\\analyze\\client.py");
    // CString Path = strPath + _T("\\python\\controller\\controller.exe");  // exe
    //CString Path = strPath + _T("\\dist\\analyze\\analyze.exe");  // exe
    
    std::string command;
    // CString -> narrow std::string 변환 후 연결
    command = "python ";  // 디버깅용

    CT2A asciiPath(Path);
    command += std::string(asciiPath);

    // 만약 exe 배포판이라면: "dist\\analyze\\analyze.exe";

    for (const auto& path : pData->imagePaths) {
        command += " \"" + path + "\"";
    }

    // 2. 파이프 및 프로세스 생성 (SW_HIDE로 창 숨김)
    SECURITY_ATTRIBUTES saAttr = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &saAttr, 0)) {
        delete pData; return 0;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite; // 에러도 캡쳐
    si.wShowWindow = SW_HIDE; // [중요] 검은 창 숨기기

    PROCESS_INFORMATION pi = { 0 };
    std::vector<char> cmdBuf(command.begin(), command.end());
    cmdBuf.push_back('\0');

    if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {

        // 프로세스 핸들을 다이얼로그 멤버 변수에 저장 (Reset 버튼이 죽일 수 있게)
        if (pDlg) {
            HANDLE hDup = NULL;
            BOOL bSuccess = ::DuplicateHandle(
                ::GetCurrentProcess(),    // 현재 프로세스 (나)
                pi.hProcess,              // 복사할 핸들 (파이썬 프로세스)
                ::GetCurrentProcess(),    // 복사받을 프로세스 (나)
                &hDup,                    // 저장할 곳
                0,
                FALSE,
                DUPLICATE_SAME_ACCESS
            );

            if (bSuccess) {
                pDlg->m_hRunningProcess = hDup; // 복사된 핸들 저장
            }
        }
        CloseHandle(hWrite); // 쓰기 핸들 닫기 (파이프 끝 인식용)

        // 3. 출력 읽기 루프
        char buffer[1024];
        DWORD bytesRead;
        std::string lineBuffer;

        while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            lineBuffer += buffer;

            size_t pos;
            while ((pos = lineBuffer.find('\n')) != std::string::npos) {
                std::string line = lineBuffer.substr(0, pos);
                
                // '\r' 제거
                if (!line.empty() && line.back() == '\r') line.pop_back();

               // [중요] 메인 스레드에게 "화면 갱신해줘"라고 쪽지 보내기
                // CString을 힙에 할당해서 주소를 보냄 (받는 쪽에서 delete 필수)
                CString* pMsg = new CString(line.c_str());
                ::PostMessage(pData->hWnd, WM_THREAD_UPDATE_MSG, 0, (LPARAM)pMsg);

                finalResult = line; // 마지막 줄 저장
                lineBuffer.erase(0, pos + 1);
            }
            // 마지막 출력 
            //::MessageBox(NULL, CA2T(finalResult.c_str()), _T("python 출력"), MB_OK);
        }
        // 루프가 끝나면(작업 종료) 핸들 변수 초기화
        if (pDlg) {
            pDlg->m_hRunningProcess = NULL;
        }
        CloseHandle(hRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else {
        CloseHandle(hWrite);
        CloseHandle(hRead);
    }

    // 4. 작업 완료 통보
    // 마지막 결과값(경로)을 담아서 보냄
    CString* pFinalRes = new CString(finalResult.c_str());
    ::PostMessage(pData->hWnd, WM_THREAD_FINISHED, 0, (LPARAM)pFinalRes);

    delete pData; // 구조체 해제
    return 0;
}

// 파일 경로 저장 변수
CString selectedFilePath;

// 파일 선택 버튼
void CMFCApplication2Dlg::OnBnClickedButtonSelect()
{
    CFileDialog dlg(TRUE, _T("jpg"), NULL,
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT,
        _T("Image Files (*.jpg;*.png;*.bmp;*.jpeg;*.tif)|*.jpg;*.png;*.bmp;*.jpeg;*.tif|All Files (*.*)|*.*||"));

    dlg.m_ofn.nMaxFile = 1024; // 다중 선택을 위한 버퍼 크기 증가
    TCHAR szFile[1024] = { 0 };
    dlg.m_ofn.lpstrFile = szFile;

    if (dlg.DoModal() == IDOK) {
        selectedFilePaths.RemoveAll(); // 기존 경로 초기화

        POSITION pos = dlg.GetStartPosition();

        while (pos) {
            CString filePath = dlg.GetNextPathName(pos);
            selectedFilePaths.Add(filePath);
        }

        // 선택한 파일 경로를 UI에 표시 (한 줄씩)
        CString paths;
        for (int i = 0; i < selectedFilePaths.GetSize(); i++) {
            paths += selectedFilePaths[i] + _T("\r\n");
        }

        SetDlgItemText(IDC_STATIC_PATH, paths);
    }
}

// 분석 버튼
void CMFCApplication2Dlg::OnBnClickedButtonAnalyze()
{
    if (selectedFilePaths.GetSize() == 0) {
        MessageBox(_T("먼저 이미지를 선택하세요."), _T("오류"), MB_OK | MB_ICONERROR);
        return;
    }

    // 1. 스레드에 전달할 파라미터(가방) 싸기
    ThreadParam* pParam = new ThreadParam;
    pParam->hWnd = this->GetSafeHwnd(); // 내 윈도우 주소
    
    for (int i = 0; i < selectedFilePaths.GetSize(); i++) {
        pParam->imagePaths.emplace_back(CT2A(selectedFilePaths[i]));
    }
    //// [추가] 프로그래스 바 초기화
    //m_progress.SetRange(0, 100); // 0% ~ 100%
    //m_progress.SetPos(0);        // 시작은 0에서

    // 2. UI 초기화 (로딩 시작)
    SetDlgItemText(IDC_STATIC_RESULT, _T("시스템 초기화 중"));
    GetDlgItem(IDC_BUTTON_ANALYZE)->EnableWindow(FALSE); // 중복 실행 방지 버튼 비활성

    // 3. 워커 스레드 시작 (AfxBeginThread)
    AfxBeginThread(PythonThreadProc, pParam);
}

// 결과 폴더 버튼
void CMFCApplication2Dlg::OnBnClickedButtonCheck()
{
    TCHAR szPath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, szPath, MAX_PATH);

    // 2. 파일 이름(예: Application.exe)을 제거하고 디렉토리만 남김
    CString strPath(szPath);
    int nPos = strPath.ReverseFind('\\'); // 마지막 '\' 위치 찾기
    if (nPos > 0)
        strPath = strPath.Left(nPos); // '\' 이전까지만 자름 (예: C:\Users\yy\Desktop\test)

    // 3. 상대 경로 연결 (현재 폴더 안에 results/prediction이 있다고 가정)
    // CString checkPath = strPath + _T("\\results\\prediction"); 
    CString checkPath = strPath + _T("\\..\\..\\..\\results\\prediction");

    // ---------------------------------------------------------
    // 디버깅용: 경로가 맞는지 확인 (개발 중에만 켜세요)
    //MessageBox(checkPath, _T("확인된 경로"), MB_OK);
    // ---------------------------------------------------------
    
    CString resPathCStr = checkPath;
    ImageDialog imagedlg(resPathCStr);
    imagedlg.DoModal();
}

// 초기화 버튼
void CMFCApplication2Dlg::OnBnClickedButtonReset()
{
    // [확실한 방법] 윈도우 시스템 명령어로 'client.exe'라는 이름의 모든 프로세스 + 자식들까지 강제 종료
    // /F: 강제 종료
    // /T: 자식 프로세스까지 트리로 종료 (이게 중요!)
    // /IM: 이미지 이름으로 찾기
    // > nul 2>&1: 검은 창이나 결과 메시지 숨기기
    if (m_hRunningProcess != NULL) {
        // 핸들로 강제 종료
        ::TerminateProcess(m_hRunningProcess, 0);

        // [중요] 복사했던 핸들이므로 반드시 닫아줘야 함
        ::CloseHandle(m_hRunningProcess);
        m_hRunningProcess = NULL;
    }

    // 2. UI 및 데이터 초기화
    selectedFilePaths.RemoveAll();
    selectedFilePath = _T("");

    SetDlgItemText(IDC_STATIC_PATH, _T("파일을 선택하세요."));
    SetDlgItemText(IDC_STATIC_RESULT, _T("분석 작업이 취소되었습니다."));

    GetDlgItem(IDC_BUTTON_ANALYZE)->EnableWindow(TRUE);
}

// [메시지 처리 1] 진행 상황 업데이트
LRESULT CMFCApplication2Dlg::OnThreadUpdate(WPARAM wParam, LPARAM lParam)
{
    // 스레드가 보낸 CString 포인터를 받음
    CString* pMsg = (CString*)lParam;
    
    // UI 갱신 (SetDlgItemText는 메인 스레드에서만 안전함)
    if (pMsg) {
        CString strMsg = *pMsg;
        SetDlgItemText(IDC_STATIC_RESULT, strMsg);
        
    // 1. 진행률 처리 (@PROGRESS:)
        // int findProgress = strMsg.Find(_T("@PROGRESS:"));
        // if (findProgress != -1) {  // 암호 발견
        //     // 예: "@PROGRESS:50" -> "50" 부분만 잘라냄
        //     CString strNum = strMsg.Mid(findProgress + 10);  // "@PROGRESS:" 길이가 10글자
        //     // 숫자로 변환
        //     int percent = _ttoi(strNum);
        //     // 프로그래스 바 움직이기
        //     if (percent >= 0 && percent <= 100) {
        //         m_progress.SetPos(percent);
        //     }
        // }
        // // 2. 일반 로그 처리
        // else {
        //     SetDlgItemText(IDC_STATIC_RESULT, strMsg);
        // }

        delete pMsg; // 메모리 해제
    }
    return 0;
}

// [메시지 처리 2] 작업 완료 처리
LRESULT CMFCApplication2Dlg::OnThreadFinished(WPARAM wParam, LPARAM lParam)
{
    CString* pResult = (CString*)lParam;
    
    // 버튼 다시 활성화
    GetDlgItem(IDC_BUTTON_ANALYZE)->EnableWindow(TRUE);

    if (pResult) {
        // 결과 파싱 로직 (기존 코드 재사용)
        // 예: 12/C:\Path\Result.png
        std::string resStr = CT2A(*pResult);
        
        std::string resCnt, resPath;
        std::istringstream ss(resStr);
        std::getline(ss, resCnt, '/');
        std::getline(ss, resPath);

        CString pathText(resPath.c_str());
        
        // 결과 이미지 띄우기
        if (!pathText.IsEmpty()) {
            ImageDialog imagedlg(pathText);
            imagedlg.DoModal();
        } else {
            MessageBox(_T("분석 결과가 올바르지 않습니다."), _T("오류"), MB_OK);
        }

        delete pResult; // [중요] 메모리 해제
    }
    return 0;
}

CMFCApplication2Dlg::CMFCApplication2Dlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_MFCAPPLICATION2_DIALOG, pParent)
    , m_hRunningProcess(NULL) // [추가] NULL로 초기화
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

BOOL CMFCApplication2Dlg::OnInitDialog()
{
    CDialogEx::OnInitDialog(); // 부모 클래스의 OnInitDialog 호출

    // 다이얼로그 초기화 작업 (예: 아이콘 설정)
    SetIcon(m_hIcon, TRUE);  // 큰 아이콘
    SetIcon(m_hIcon, FALSE); // 작은 아이콘

    // 1. Job Object 생성 ("서버 관리용 그룹")
    m_hJob = CreateJobObject(NULL, NULL);

    // 2. 규칙 설정: "이 그룹(Job)의 주인이 죽으면, 멤버들도 다 죽여라"
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = { 0 };
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(m_hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));

    // 3. 서버 실행 준비
    TCHAR szPath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, szPath, MAX_PATH);
    CString strPath(szPath);
    int nPos = strPath.ReverseFind('\\');
    if (nPos > 0) strPath = strPath.Left(nPos);

    // CString serverPath = strPath + _T("\\python\\server\\server.exe");
    // 개발 중일 때: 
    CString serverPath = strPath + _T("\\..\\..\\..\\dist\\analyze\\server.py");

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // 서버 창 숨김

    // std::string cmd = CT2A(serverPath);
    std::string cmd = "python ";
    cmd += CT2A(serverPath);
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    // 4. 서버 프로세스 생성 (중요: CREATE_SUSPENDED로 '일시정지' 상태로 만듦)
    // 이유: 실행되자마자 도망가기 전에 Job에 등록해야 하니까
    if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        // 5. 서버를 Job에 등록 (감옥에 넣음)
        AssignProcessToJobObject(m_hJob, pi.hProcess);

        // 6. 이제 실행해라 (일시정지 해제)
        ResumeThread(pi.hThread);

        // 핸들 정리 (Job이 관리하므로 우리는 핸들 갖고 있을 필요 없음)
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else
    {
        MessageBox(_T("서버 실행 실패"), _T("Error"), MB_OK);
    }
    // --------------------------------------------------------

    return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE, 설정하면 FALSE
}



BEGIN_MESSAGE_MAP(CMFCApplication2Dlg, CDialogEx)
    ON_BN_CLICKED(IDC_BUTTON_SELECT, &CMFCApplication2Dlg::OnBnClickedButtonSelect)
    ON_BN_CLICKED(IDC_BUTTON_ANALYZE, &CMFCApplication2Dlg::OnBnClickedButtonAnalyze)
    ON_BN_CLICKED(IDC_BUTTON_RESET, &CMFCApplication2Dlg::OnBnClickedButtonReset)
    ON_EN_CHANGE(IDC_STATIC_PATH, &CMFCApplication2Dlg::OnEnChangeStaticPath)
    ON_BN_CLICKED(IDC_BUTTON_CHECK, &CMFCApplication2Dlg::OnBnClickedButtonCheck)
    // [추가] 사용자 정의 메시지 연결
    ON_MESSAGE(WM_THREAD_UPDATE_MSG, &CMFCApplication2Dlg::OnThreadUpdate)
    ON_MESSAGE(WM_THREAD_FINISHED, &CMFCApplication2Dlg::OnThreadFinished)
    ON_STN_CLICKED(IDC_STATIC_RESULT, &CMFCApplication2Dlg::OnStnClickedStaticResult)
    ON_WM_DESTROY()
END_MESSAGE_MAP()

void CMFCApplication2Dlg::OnDestroy()
{
    CDialogEx::OnDestroy();

    // [서버 종료 로직]
    // taskkill 명령어 필요 없음! 
    // 그냥 Job 핸들만 닫으면 윈도우가 알아서 서버 프로세스 트리를 몰살시킵니다.

    if (m_hJob != NULL)
    {
        CloseHandle(m_hJob);
        m_hJob = NULL;
    }

    // 만약 클라이언트가 돌고 있었다면 그것도 정리
    if (m_hRunningProcess != NULL)
    {
        TerminateProcess(m_hRunningProcess, 0);
        CloseHandle(m_hRunningProcess);
        m_hRunningProcess = NULL;
    }

    // TODO: 여기에 메시지 처리기 코드를 추가합니다.
}
// -----------------------------------------------------------
// [링커 에러 방지용 빈 함수 추가]
// 헤더에 선언된 함수는 반드시 본문이 있어야 합니다.
// -----------------------------------------------------------

void CMFCApplication2Dlg::OnEnChangeStaticPath()
{
    // 경로 표시 텍스트가 변경될 때 호출됨 (현재는 기능 없음)
}

void CMFCApplication2Dlg::OnStnClickedStaticResult()
{
    // 결과 텍스트를 클릭했을 때 호출됨 (현재는 기능 없음)
}