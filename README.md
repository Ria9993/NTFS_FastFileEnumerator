# NTFS_FastFileEnumerator
윈도우 NTFS(New Technology File System)의 모든 파일 목록을 고성능으로 열거합니다.

# Note
`NTFS_Structure.h`를 포함한 해당 프로젝트의 모든 소스코드는 직접 작성된 코드입니다.   
따라서 기술적 유효성을 보장하지 않으며 각 버젼에 대한 필드 값 검증이 동반되어야 합니다.  


# Development Log
### 2024-06-11  
WinAPI는 너무 느려서, 물리 디스크 자체를 바이너리로 읽어와서 분석하기로 결정.  
파일 시스템이나 NTFS에 대한 지식이 없어서 명세를 찾아보기 시작.  
MS의 NTFS에 대한 자료 공개가 적어서 어려움이 있음.  

성능에 대해선 Memory Mapped File이나 non-temporal load를 생각 중.  
그리고 최근 접근한 MFT(Master File Table)을 4KB 페이지 단위로 트래킹해서,  
최근 접근한 페이지가 아니면 지연시키는 방법도 생각했으나 MFT entry 자체가 1KB라서 별로 소용 없을거라 생각하고 드랍.  

### 2024-06-13
구현 자체는 하루 걸림.  
일단 기본 구현은 끝났는데, $FILENAME 타입이 POSIX namespace인 경우 파일이름이 깨지는 문제가 있음.  
그리고 성능 올릴 방법도 생각해야 되는데 일단 나중에  

https://github.com/Ria9993/NTFS_FastFileEnumerator/assets/44316628/d082e207-e1d3-4a60-842b-676d594be01a


### 2024-06-15
멀티스레드로 대충 구현해둠.  
프로파일링 해봤을 때, 파일IO 자체가 오래 걸리는게 아니였어서  
인코딩도 별도 처리없이 바이너리로 저장하게 바꾸고  
거의 1~4초로 6배 가량 빨라짐  

https://github.com/Ria9993/NTFS_FastFileEnumerator/assets/44316628/57ad570a-6a7d-456f-83ec-600f9bd20667

## 2024-06-17
1TB SSD 파일 250만개에서 대충 5.9초 정도 소요되는데  
50%가 파일 읽을 때의 버퍼 할당이라 파일을 맵핑해서 쓰는게 나아보임.  
시간날 때 수정할 예정  
![image](https://github.com/Ria9993/NTFS_FastFileEnumerator/assets/44316628/a31c3c4a-305b-4879-b750-3b74c6b77630)

# Reference  
MS는 독점 기술 NTFS에 대한 스펙 문서를 공개 및 업데이트하지 않고 있습니다.  
따라서 파편화된 공식 문서들과 Linux진영의 NTFS 라이브러리, 포렌식 커뮤니티의 비공식 서드파티 문서를 참조합니다.  
문서의 모든 명세 사항은 컴파일타임과 런타임에서의 검증이 반드시 동반되어야 합니다.  

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
