﻿// dllmain.cpp : 定义 DLL 应用程序的入口点。
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "rapidjson/document.h"     // rapidjson's DOM-style API
#include "rapidjson/prettywriter.h" // for stringify JSON
#include "rapidjson/pointer.h"
#include <WinSock2.h>
#include <iostream>
#include <stdio.h>
#pragma comment(lib,"ws2_32.lib")
#include "pch.h"
#include "Utils.h"
#include "AddrOffset.h"
#include <vector>
#include <queue>
DWORD WINAPI ThreadProc(PVOID lpParameter);

using namespace rapidjson;
using namespace std;


std::queue<string> gSendQue;

/****************** 1、socket 开始*****************/

#define nSerPort 10080
#define nBufMaxSize 1024

BOOL InitSocket();
SOCKET ConnectSocket();
BOOL CloseConnect(SOCKET sd);


/****************** 1、Socket 结束*****************/



/****************** 2、业务代码 开始*****************/

DWORD g_WinBaseAddress = 0;

DWORD getWeChatWinAddr();

BOOL SentTextMessage(wchar_t* wxid, wchar_t* content);


//接收消息
CHAR originalCode[5] = { 0 };
DWORD g_moveAddr = 0x66553C50;
DWORD g_hookOffsetAddr = 0x3CD5A5;
DWORD g_jumBackOffsetAddr = 0x3CD5AB;
DWORD g_jumBackAddr = 0;
VOID HookWx();
VOID RecieveMsg();
VOID RecieveMsgHook();
DWORD g_esp = 0;
void WSSendMsg(DWORD msgType, wchar_t* msg, wchar_t* fromWxid, wchar_t* roomWxid, wchar_t* source);

/****************** 业务代码 结束*****************/


VOID __declspec(dllexport) Test()
{
	//OutputDebugString(TEXT("开始调试"));
}

//1111111111111111111111
typedef struct {
	VOID(*RECEIVE)(DWORD, LPSTR, DWORD);
	VOID(*ACCEPT)(DWORD);
	VOID(*CLOSE)(DWORD);
} CallBackFun;
CallBackFun* fun;
SOCKET g_hClient;
/************* Socket 开始**************/

void MyTcpServeFun();
DWORD WINAPI ThreadProcsend(PVOID lpParameter)
{
	while (1)
	{
		Sleep(3000);
		if (gSendQue.size() > 0)
		{
			string strSend = gSendQue.front();

			gSendQue.pop();

			int n = send(g_hClient, strSend.c_str(), strSend.length(), 0);
			int n1 = WSAGetLastError();
			int x = 1;

		}
	}
}

//新线程，开启socket
DWORD WINAPI ThreadProc(PVOID lpParameter) {

	DebugLog(TEXT("ThreadProc...\n "));

	//初始化
	InitSocket();

	HANDLE hThread = CreateThread(NULL, 0, ThreadProcsend, NULL, 0, NULL);
	//业务处理
	MyTcpServeFun();

	////关闭webSocket链接
	//WSACleanup();

	return 0;

}

BOOL InitSocket() {

	WSADATA wsaData;
	SOCKET hServer;
	WORD wVerSion = MAKEWORD(2, 2);
	if (WSAStartup(wVerSion, &wsaData)) {
		//如果启动失败
		DebugLog((char*)"initSocket->WSAStartup error");
		return FALSE;
	};

	return TRUE;
}

//先创建socket，绑定本地地址，然后开始监听
SOCKET BindListen(int nBackLog) {

	//流式套接字SOCK_STREAM，  TCP协议 IPPROTO_TCP
	SOCKET hServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (hServer == INVALID_SOCKET) {

		DebugLog((char*)"bind listen->socket error");
		return INVALID_SOCKET;
	}

	sockaddr_in addrServer;
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(10080);
	addrServer.sin_addr.s_addr = htonl(INADDR_ANY);

	//绑定
	int nRet = bind(hServer, (sockaddr*)&addrServer, sizeof(addrServer));
	if (nRet == SOCKET_ERROR) {
		DebugLog((char*)"socket 绑定失败");
		closesocket(hServer);
		return INVALID_SOCKET;
	}

	//在socket上进行监听
	if (listen(hServer, 10) == SOCKET_ERROR) {
		closesocket(hServer);
		WSACleanup();
		DebugLog((char*)"listen错误");
		return INVALID_SOCKET;
	}

	return hServer;
}

SOCKET AcceptConnetion(SOCKET hSocket) {

	sockaddr_in saConnAddr;
	int nSize = sizeof(saConnAddr);

	SOCKET hClient = accept(hSocket, (LPSOCKADDR)&saConnAddr, &nSize);
	if (hClient == INVALID_SOCKET) {
		DebugLog((char*)"AcceptConnetion 错误");
		return INVALID_SOCKET;
	}


	return hClient;
}

BOOL ClientConFun(SOCKET sd) {

	DebugLog(TEXT("ClientConFun -> enter"));

	char receiveBuf[nBufMaxSize];
	int nRetByte;

	//RapidJson
	Document doc;
	doc.SetObject();
	Document::AllocatorType& allocator = doc.GetAllocator(); //获取分配器

	while (true) {
		//nRetByte：读取长度，直到遇到0
		nRetByte = recv(sd, receiveBuf, nBufMaxSize, 0);
		if (nRetByte == SOCKET_ERROR) {
			//debugLog((char*)"AcceptConnetion 错误");
			OutputDebugString(TEXT("WeChatHelperERROR.. "));
			return FALSE;
		}
		else if (nRetByte != 0) {

			if (doc.ParseInsitu(receiveBuf).HasParseError()) {
				OutputDebugString(TEXT("ParseInsitu is error...\n"));
				continue;
			}

			//解析type
			int nType = doc["type"].GetInt();
			switch (nType)
			{
			case MT_SEND_TEXTMSG:
			{
				//发送文本消息
				wchar_t* wToWxid = StrToWchar(doc["data"]["to_wxid"].GetString());
				wchar_t* wContent = StrToWchar(doc["data"]["content"].GetString());

				DebugLog(wToWxid);
				DebugLog(wContent);

				if (!SentTextMessage(wToWxid, wContent)) {
					//TODO 发送失败
					DebugLog(TEXT("发送消息失败\n"));
				};
				break;
			}
			default:
				break;
			}
		}
	}

	return TRUE;
}

//发送消息到客户端
BOOL SocketSendMsg(SOCKET sd, char* buf, int nSize) {

	int nTemp = send(sd, buf, nSize, 0);
	if (nTemp > 0) {
		return TRUE;
	}
	else if (nTemp == SOCKET_ERROR) {
		DebugLog((char*)"ClientConFun -> send 错误 \n");
		return FALSE;
	}
	else
	{
		//send返回0，由于此时send < nRetByte，也就是数据还没发送出去，表示客户端被意外被关闭了
		DebugLog((char*)"ClientConFun -> send -> close 错误 \n");
		return FALSE;
	}

}

//关闭一个链接
BOOL CloseConnect(SOCKET sd) {

	//首先发送一个TCP FIN分段，向对方表明已经完成数据发送
	if (shutdown(sd, SD_SEND) == SOCKET_ERROR) {

		DebugLog((char*)"CloseConnect - > ShutDown error");
		return FALSE;
	}
	char buf[nBufMaxSize];
	int nRetByte;

	do {
		nRetByte = recv(sd, buf, nBufMaxSize, 0);
		if (nRetByte == SOCKET_ERROR) {
			DebugLog((char*)"closeconnect -> recv error");
			break;
		}
		else if (nRetByte > 0) {

			DebugLog((char*)"closeconnect 错误的接收数据");
			break;
		}

	} while (nRetByte != 0);

	if (closesocket(sd) == SOCKET_ERROR) {
		DebugLog((char*)"closeconnect -> closesocket error");
		return FALSE;
	}
	return TRUE;
}

void MyTcpServeFun() {

	SOCKET hSocket = BindListen(1);
	if (hSocket == INVALID_SOCKET) {
		DebugLog((char*)"MyTcpSerFun -> bindListen error");
		return;
	}
	while (true) {

		SOCKET hClient = AcceptConnetion(hSocket);

		if (hClient == INVALID_SOCKET) {
			DebugLog((char*)"MyTcpSerFun -> acceptconnect error");
			break;
		}

		g_hClient = hClient;

		//客户端处理
		if (ClientConFun(hClient) == FALSE) {
			//break;
		}

		//关闭一个客户端连接
		if (CloseConnect(hClient) == FALSE) {
			//break;
		}

	}

	if (closesocket(hSocket) == SOCKET_ERROR) {
		DebugLog((char*)"mytcpSerFun --> closesocket");
		return;
	}
}

/************* Socket 结束**************/


/****************** 回调 开始*****************/

BOOL InitWeChatSocket(VOID(*RECEIVE)(DWORD, LPSTR, DWORD), VOID(*ACCEPT)(DWORD), VOID(*CLOSE)(DWORD)) {

	fun = (CallBackFun*)malloc(sizeof(CallBackFun));
	fun->RECEIVE = RECEIVE;
	fun->ACCEPT = ACCEPT;
	fun->CLOSE = CLOSE;


	//开启socket服务
	HANDLE hThread = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);
	//if (hThread) {
	//	CloseHandle(hThread);
	//}

	return TRUE;

}

/****************** 回调 结束*****************/



VOID receive(DWORD clientId, LPSTR data, DWORD len)
{
	//std::cout << "new message! " << data << endl;
	std::cout << data << endl;
}
VOID accept(DWORD clientId)
{
	std::cout << "new client! " << endl;
}
VOID close(DWORD clientId)
{
	std::cout << "client closed! " << endl;
}

//1111111111111111111111111111

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{

		DebugLog(TEXT("WeChatHelper "));

		//hook接收消息
		HookWx();


		InitWeChatSocket(receive, accept, close);
		//         HANDLE hThread = CreateThread(NULL, 0, ThreadProc, NULL, 0, NULL);
		//         if (hThread) {
		//             CloseHandle(hThread);
		//         }

	}
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}



/****************** 1、socket 开始*****************/
//新线程，开启socket

SOCKET ConnectSocket() {

	SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);

	SOCKADDR_IN clientsock_in;
	clientsock_in.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
	clientsock_in.sin_family = AF_INET;
	clientsock_in.sin_port = htons(10080);
	connect(clientSocket, (SOCKADDR*)&clientsock_in, sizeof(SOCKADDR));//开始连接
	//int n = send(clientSocket, "123", 3, 0);
	return clientSocket;
}

/******************socket 结束*****************/



/****************** 2、业务代码 开始*****************/


//文本消息结构体
struct StructWxid
{
	//发送的文本消息指针
	wchar_t* pWxid;
	//字符串长度
	DWORD length;
	//字符串最大长度
	DWORD maxLength;

	//补充两个占位数据
	DWORD fill1;
	DWORD fill2;
};

//发送文本消息
BOOL SentTextMessage(wchar_t* wxid, wchar_t* content) {


	//定位发送消息的Call的位置
	DWORD callFunAddr = getWeChatWinAddr() + SEND_MSG_HOOK_ADDRESS;

	//组装wxid数据
	StructWxid structWxid = { 0 };
	structWxid.pWxid = wxid;
	structWxid.length = wcslen(wxid);
	structWxid.maxLength = wcslen(wxid) * 2;
	//取wxid的地址
	DWORD* asmWxid = (DWORD*)&structWxid.pWxid;

	//组装content数据
	StructWxid structMessage = { 0 };
	structMessage.pWxid = content;
	structMessage.length = wcslen(content);
	structMessage.maxLength = wcslen(content) * 2;

	//取msg的地址
	DWORD* asmMsg = (DWORD*)&structMessage.pWxid;

	//定义一个缓冲区
	BYTE buff[SEND_MSG_BUFFER] = { 0 };

	DebugLog(TEXT("SentTextMessage -> pre send\n"));
	DebugLog(wxid);
	DebugLog(content);

	__asm
	{
		push 0x1
		mov edi, 0x0
		push edi

		mov ebx, asmMsg
		push ebx

		mov edx, asmWxid
		lea ecx, buff

		call callFunAddr
		add esp, 0xC
	}

	return TRUE;

}


//Hook接收消息
VOID HookWx()
{


	//WeChatWin.dll+354AA3 
	int hookAddress = getWeChatWinAddr() + g_hookOffsetAddr;

	g_jumBackAddr = getWeChatWinAddr() + g_jumBackOffsetAddr;

	DWORD addr = getWeChatWinAddr();

	string text = "微信基址：\t";
	text.append(Dec2Hex(addr));
	DebugLog(String2LPCWSTR(text));


	//组装跳转数据
	BYTE jmpCode[5] = { 0 };
	jmpCode[0] = 0xE9;

	//新跳转指令中的数据=跳转的地址-原地址（HOOK的地址）-跳转指令的长度
	*(DWORD*)&jmpCode[1] = (DWORD)RecieveMsgHook - hookAddress - 5;

	//保存当前位置的指令,在unhook的时候使用。
	//ReadProcessMemory(GetCurrentProcess(), (LPVOID)hookAddress, originalCode, 5, 0);

	//覆盖指令 B9 E8CF895C //mov ecx,0x5C89CFE8
	WriteProcessMemory(GetCurrentProcess(), (LPVOID)hookAddress, jmpCode, 5, 0);

	DebugLog(TEXT("HookWx...111"));
}

//跳转到这里，让我们自己处理消息
__declspec(naked) VOID RecieveMsgHook()
{

	__asm
	{
		//执行wx程序代码
		mov ecx, g_moveAddr
		push edi

		//提取esp寄存器内容，放在一个变量中
		mov g_esp, esp

		//保存寄存器
		pushad
		pushf
	}

	//调用接收消息的函数
	RecieveMsg();


	//恢复现场
	__asm
	{
		popf
		popad

		//跳回 66A0D50B    FF50 08         call dword ptr ds:[eax+0x8]
		jmp g_jumBackAddr
	}
}

VOID RecieveMsg()
{

	wchar_t* fromWxid = { 0 };
	wchar_t* roomWxid = { 0 };
	wchar_t* content = { 0 };
	wchar_t* source = { 0 };

	DWORD** msgAddress = (DWORD**)g_esp;

	//消息类型
	DWORD msgTypeOffset = 0x30;
	DWORD msgType = *((DWORD*)(**msgAddress + msgTypeOffset));

	////只处理文本和文件消息
	//if (msgType != 0x01 && msgType != 0x31) {
	//    return;
	//}

	// 消息发送者，联系人微信ID或群ID
	fromWxid = GetMsgByAddress2(**msgAddress + 0x40);
	// 如果是群消息，发送人微信ID
	roomWxid = GetMsgByAddress2(**msgAddress + 0x164);
	// 消息内容
	content = GetMsgByAddress2(**msgAddress + 0x68);

	WSSendMsg(msgType, content, fromWxid, roomWxid, source);


}

void WSSendMsg(DWORD msgType, wchar_t* msg, wchar_t* fromWxid, wchar_t* roomWxid, wchar_t* source) {


	Document document;
	Pointer("/event").Set(document, "RECV_TEXT_MSG");
	Pointer("/msg").Set(document, UnicodeToUtf8(msg));
	Pointer("/fromWxid").Set(document, UnicodeToUtf8(fromWxid));
	Pointer("/senderWxid").Set(document, UnicodeToUtf8(roomWxid));
	Pointer("/source").Set(document, UnicodeToUtf8(source));
	Pointer("/msgType").Set(document, (int)msgType);

	StringBuffer buffer;
	PrettyWriter<StringBuffer> writer(buffer);
	document.Accept(writer);
	string jsonStr = buffer.GetString();

	size_t nSize = buffer.GetSize();

	//发送socket
//     int n = send(g_hClient, "123", 3, 0);
// 	int n1 = WSAGetLastError();

	gSendQue.push(jsonStr);

}

/******************业务代码 结束*****************/



/*
	获取WeChatWin基址
 */
DWORD getWeChatWinAddr()
{
	DWORD addr = 0;
	if (g_WinBaseAddress == 0)
	{
		addr = (DWORD)LoadLibrary(L"WeChatWin.dll");
		return addr;
	}
	else
	{
		return g_WinBaseAddress;
	}

}
