kdesktop
=================

kDesktop(com.leagor.kdesktop)是使用微软RDP协议的开源远程桌面客户端。


如何使用kDesktop参考“Launcher”: <https://www.cswamp.com/post/23>

## 编译
kdesktop基于Rose SDK，此处存放的是kdesktop私有包。为此需先下载Rose：<https://github.com/freeors/Rose>。运行studio，在左上角鼠标右键，弹出菜单“导入...”，选择kdesktop-res。

## 开源协议及开源库
kdesktop自个源码的开源协议是BSD。当中用到不少开源软件，主要有FreeRDP、SDL、Chromium和BoringSSL，它们有在用不是BSD的开源协议。理论上说，FreeRDP就包括了网络部分，但kDesktop网络部分使用Chromium，FreeRDP已和网络收发无关了。因为使用Chromium，加密采用BoringSSL，没有再用OpenSSL。