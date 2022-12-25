# Sogang-Operating-System

운영체제 과목 기준 작성

### project 1
system call and parent-child process synchronization

### project 2
basic file system call
- synchronization 부분에서 project3~5 때 수정될 부분이 몇몇 존재한다.

### project 3
priority scheduling
- project 4의 시작 부분에서 project 3가 project 1,2과 호환되지 않는 오류 부분을 수정하였다.

### project 4
virtual memory(paging)
- 똑같은 read-only code 영역에 대해 memory sharing을 지원할 수 있지만,    
  실질적으로 구현되지는 않은 상태이고 구현할 생각은 없다.     
  즉, memory sharing 코드가 작성되어 있지만 의미 없는 부분이고,     
  virtual page와 user pool frame은 1:1 대응된다.

### project 5
file system
- 참조 코드