# NTFS_FastFileEnumerator
윈도우 NTFS(New Technology File System)의 모든 파일 목록을 고성능으로 열거합니다.

# Development Log
### 2024-06-11  
WinAPI는 너무 느려서, 물리 디스크 자체를 바이너리로 읽어와서 분석하기로 결정.  
파일 시스템이나 NTFS에 대한 지식이 없어서 명세를 찾아보기 시작.  
MS의 NTFS에 대한 자료 공개가 적어서 어려움이 있음.  

성능에 대해선 Memory Mapped File이나 non-temporal load를 생각 중.  
그리고 최근 접근한 MFT(Master File Table)을 4KB 페이지 단위로 트래킹해서,  
최근 접근한 페이지가 아니면 지연시키는 방법도 생각했으나 MFT entry 자체가 1KB라서 별로 소용 없을거라 생각하고 드랍.  
[Reference](

### 2024-06-13
구현 자체는 하루 걸림.  
일단 기본 구현은 끝났는데, $FILENAME 타입이 POSIX namespace인 경우 파일이름이 깨지는 문제가 있음.  
그리고 성능 올릴 방법도 생각해야 되는데 일단 나중에  

https://github.com/Ria9993/NTFS_FastFileEnumerator/assets/44316628/7c92d09b-41cf-43dc-a03a-4510290324b4  

# Reference
### MSDN (Not trusted because regular updates are stopped)
- BIOS/MBR, UEFI/GPT structure  
https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2003/cc739412(v=ws.10)
- NTFS Archiecture overview  
https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2003/cc781134(v=ws.10)?redirectedfrom=MSDN
### Third-party (Not trusted for actual implementation)
- NTFS reference third-party documantation  
https://github.com/libyal/libfsntfs/blob/main/documentation/New%20Technologies%20File%20System%20(NTFS).asciidoc
- MFT(Master File Table) third-party cheat sheet  
https://github.com/tpn/pdfs/blob/master/NTFS%20Cheat%20Sheet.pdf  
