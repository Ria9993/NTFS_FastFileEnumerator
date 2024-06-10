# NTFS_FastFileEnumerator
윈도우 NTFS(New Technology File System)의 모든 파일 목록을 고성능으로 열거합니다.

# Caution
개발 중

# Development Log
- 2024-06-11  
WinAPI는 너무 느려서, 물리 디스크 자체를 바이너리로 읽어와서 분석하기로 결정.  
파일 시스템이나 NTFS에 대한 지식이 없어서 명세를 찾아보기 시작.  
MS의 NTFS에 대한 자료 공개가 적어서 어려움이 있음.  

성능에 대해선 Memory Mapped File이나 non-temporal load를 생각 중.  
그리고 최근 접근한 MFT(Master File Table)을 4KB 페이지 단위로 트래킹해서,  
최근 접근한 페이지가 아니면 지연시키는 방법도 생각했으나 MFT entry 자체가 1KB라서 별로 소용 없을거라 생각하고 드랍.  
