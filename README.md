# SimpleFS 

A simple filesystem to understand things, probably to a RTOS in the future far far away.

<b>This is a Work In Progress. Do not use.</b>
<br>
<br>

### Test
```bash
./bin/sfssh ./data/img_5.raw 5
```
<br>
<br>

## format:

test
 super | inode | indirect | data | directory
:-----:|:-----:|:--------:|:----:|:---------:
 0     | 1     | 2        | 3    | 4


boot
boot | super | inode | indirect | data | directory
:---:|:-----:|:-----:|:--------:|:----:|:---------:
0    | 1     | 2     | 3        | 4    | 5


### Refs: 
- https://www3.nd.edu/~pbui/teaching/cse.30341.fa17/project06.html
- https://rudradesai.in/SimpleFS/index.html
- http://www.zedwood.com/article/cpp-sha256-function