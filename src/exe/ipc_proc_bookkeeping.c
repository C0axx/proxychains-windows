﻿#include "includes_win32.h"
#include "log_win32.h"
#include "hookdll_win32.h"
#include "proc_bookkeeping_win32.h"

tab_per_process_t* g_tabPerProcess;
HANDLE g_hDataMutex;
tab_fake_ip_hostname_t* g_tabFakeIpHostname;

void PrintTablePerProcess()
{
	tab_per_process_t* Entry;
	tab_per_process_t* TempEntry;
	WCHAR TempBuf[400];
	WCHAR *pTempBuf = TempBuf;

	StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"TablePerProcess:\n");

	HASH_ITER(hh, g_tabPerProcess, Entry, TempEntry) {
		IpNode* IpNodeEntry;

		StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"%ls[WINPID" WPRDW L" PerProcessData] ", Entry == g_tabPerProcess ? L"" : L"\n", Entry->Data.dwPid);

		LL_FOREACH(g_tabPerProcess->Ips, IpNodeEntry) {
			tab_fake_ip_hostname_t* IpHostnameEntry;
			tab_fake_ip_hostname_t IpHostnameEntryAsKey;

			StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"%ls%ls", IpNodeEntry == g_tabPerProcess->Ips ? L"" : L", ", FormatHostPortToStr(&IpNodeEntry->Ip, sizeof(PXCH_IP_ADDRESS)));

			IpHostnameEntryAsKey.Ip = IpNodeEntry->Ip;
			IpHostnameEntryAsKey.dwOptionalPid = 0;
			IpHostnameEntry = NULL;
			HASH_FIND(hh, g_tabFakeIpHostname, &IpHostnameEntryAsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), IpHostnameEntry);

			if (IpHostnameEntry) {
				PXCH_UINT32 i;
				StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"(FakeIp,%ls - ", IpHostnameEntry->Hostname.szValue);
				for (i = 0; i < IpHostnameEntry->dwResovledIpNum; i++) {
					StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"%ls%ls", i ? L"/" : L"", FormatHostPortToStr(&IpHostnameEntry->ResolvedIps[i], sizeof(PXCH_IP_ADDRESS)));
				}
				StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L")");
			}

			IpHostnameEntryAsKey.dwOptionalPid = Entry->Data.dwPid;
			IpHostnameEntry = NULL;
			HASH_FIND(hh, g_tabFakeIpHostname, &IpHostnameEntryAsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), IpHostnameEntry);

			if (IpHostnameEntry) {
				PXCH_UINT32 i;
				StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"(ResolvedIp,%ls - ", IpHostnameEntry->Hostname.szValue);
				for (i = 0; i < IpHostnameEntry->dwResovledIpNum; i++) {
					StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L"%ls%ls", i ? L"/" : L"", FormatHostPortToStr(&IpHostnameEntry->ResolvedIps[i], sizeof(PXCH_IP_ADDRESS)));
				}
				StringCchPrintfExW(pTempBuf, _countof(TempBuf) - (pTempBuf - TempBuf), &pTempBuf, NULL, 0, L")");
			}
		}
	}

	LOGI(L"%ls", TempBuf);
}

DWORD ChildProcessExitedCallbackWorker(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
	LOCKED({
		tab_per_process_t * Entry = (tab_per_process_t*)lpParameter;
		tab_fake_ip_hostname_t IpHostnameAsKey;
		tab_fake_ip_hostname_t* IpHostnameEntry;
		IpNode* pIpNode;
		IpNode* pTmpIpNode;

		DWORD dwExitCode = UINT32_MAX;
		HASH_DELETE(hh, g_tabPerProcess, Entry);
		if (!GetExitCodeProcess(Entry->hProcess, &dwExitCode)) {
			LOGE(L"GetExitCodeProcess() error: %ls", FormatErrorToStr(GetLastError()));
		}
		LOGI(L"Child process winpid " WPRDW L" exited (%#010x).", Entry->Data.dwPid, dwExitCode);

		LL_FOREACH_SAFE(Entry->Ips, pIpNode, pTmpIpNode){
			IpHostnameAsKey.Ip = pIpNode->Ip;
			IpHostnameAsKey.dwOptionalPid = Entry->Data.dwPid;
			HASH_FIND(hh, g_tabFakeIpHostname, &IpHostnameAsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), IpHostnameEntry);
			if (IpHostnameEntry) {
				HASH_DELETE(hh, g_tabFakeIpHostname, IpHostnameEntry);
				HeapFree(GetProcessHeap(), 0, IpHostnameEntry);
			}

			if (g_pPxchConfig->dwDeleteFakeIpAfterChildProcessExits) {
				IpHostnameAsKey.dwOptionalPid = 0;
				HASH_FIND(hh, g_tabFakeIpHostname, &IpHostnameAsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), IpHostnameEntry);
				if (IpHostnameEntry) {
					HASH_DELETE(hh, g_tabFakeIpHostname, IpHostnameEntry);
					HeapFree(GetProcessHeap(), 0, IpHostnameEntry);
				}
			}

			LL_DELETE(Entry->Ips, pIpNode);
			HeapFree(GetProcessHeap(), 0, pIpNode);
		}

		HeapFree(GetProcessHeap(), 0, Entry);

		PrintTablePerProcess();

		if (g_tabPerProcess == NULL) {
			LOGI(L"All windows descendant process exited.");
			IF_WIN32_EXIT(0);
		}

		goto after_proc;
	});
}


VOID CALLBACK ChildProcessExitedCallback(
	_In_ PVOID   lpParameter,
	_In_ BOOLEAN TimerOrWaitFired
)
{
	ChildProcessExitedCallbackWorker(lpParameter, TimerOrWaitFired);
}

DWORD RegisterNewChildProcess(const REPORTED_CHILD_DATA* pChildData)
{
	LOCKED({
		tab_per_process_t* Entry;
		HANDLE hWaitHandle;
		HANDLE hChildHandle;

		LOGV(L"Before HeapAlloc...");
		Entry = (tab_per_process_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(tab_per_process_t));
		LOGV(L"After HeapAlloc...");
		if ((hChildHandle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pChildData->dwPid)) == NULL) {
			dwReturn = GetLastError();
			if (dwReturn == ERROR_ACCESS_DENIED) {
				if ((hChildHandle = OpenProcess(SYNCHRONIZE, FALSE, pChildData->dwPid)) == NULL) {
					dwReturn = GetLastError();
				} else {
					goto after_open_process;
				}
			}
			LOGC(L"OpenProcess() error: %ls", FormatErrorToStr(dwReturn));
			goto after_proc;
		}
	after_open_process:
		LOGD(L"After OpenProcess(" WPRDW L")...", hChildHandle);

		if (!RegisterWaitForSingleObject(&hWaitHandle, hChildHandle, &ChildProcessExitedCallback, Entry, INFINITE, WT_EXECUTELONGFUNCTION | WT_EXECUTEONLYONCE)) {
			dwReturn = GetLastError();
			LOGC(L"RegisterWaitForSingleObject() error: %ls", FormatErrorToStr(dwReturn));
			goto after_proc;
		}
		LOGV(L"After RegisterWaitForSingleObject...");
		Entry->Data = *pChildData;
		Entry->hProcess = hChildHandle;
		Entry->Ips = NULL;
		LOGV(L"After Entry->Data = *pChildData;");
		HASH_ADD(hh, g_tabPerProcess, Data, sizeof(pid_key_t), Entry);
		LOGV(L"After HASH_ADD");
		LOGI(L"Registered child pid " WPRDW, pChildData->dwPid);
		PrintTablePerProcess();
	});
}

DWORD QueryChildStorage(REPORTED_CHILD_DATA* pChildData)
{
	LOCKED({
		tab_per_process_t* Entry;
		HASH_FIND(hh, g_tabPerProcess, &pChildData->dwPid, sizeof(pid_key_t), Entry);
		if (Entry) {
			*pChildData = Entry->Data;
		}

		goto after_proc;
	});
}

DWORD NextAvailableFakeIp(PXCH_IP_ADDRESS* pFakeIp)
{
	static PXCH_UINT32 iSearch = 1;

	LOCKED({
		PXCH_UINT32 iSearchWhenEnter;
		tab_fake_ip_hostname_t AsKey;
		tab_fake_ip_hostname_t * Entry;
		AsKey.dwOptionalPid = 0;

		iSearchWhenEnter = iSearch;

		while (1) {
			Entry = NULL;
			IndexToIp(g_pPxchConfig, &AsKey.Ip, iSearch);
			HASH_FIND(hh, g_tabFakeIpHostname, &AsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), Entry);
			if (!Entry) break;
			iSearch++;
			if (iSearch >= ((PXCH_UINT64)1 << (32 - g_pPxchConfig->dwFakeIpRangePrefix)) - 1) {
				iSearch = 1;
			}

			if (iSearch == iSearchWhenEnter) {
				dwReturn = ERROR_RESOURCE_NOT_AVAILABLE;
				goto after_proc;
			}
		}

		LOGI(L"Next available index: " WPRDW, iSearch);
		*pFakeIp = AsKey.Ip;
		dwReturn = NO_ERROR;
	});
}

DWORD RegisterHostnameAndGetFakeIp(PXCH_IP_ADDRESS* pFakeIp, const tab_fake_ip_hostname_t* TempEntry, PXCH_UINT32 dwPid, BOOL bWillMapResolvedIpToHost)
{
	LOCKED({
		tab_fake_ip_hostname_t* FakeIpEntry;
		tab_fake_ip_hostname_t* ResolvedIpEntry;
		tab_per_process_t* CurrentProcessDataEntry;
		IpNode* pIpNode;
		DWORD dwErrorCode;
		DWORD dw;

		CurrentProcessDataEntry = NULL;
		HASH_FIND(hh, g_tabPerProcess, &dwPid, sizeof(dwPid), CurrentProcessDataEntry);

		if (CurrentProcessDataEntry == NULL) goto err_no_proc_entry;

		pIpNode = (IpNode*)HeapAlloc(GetProcessHeap(), 0, sizeof(IpNode));
		dwErrorCode = NextAvailableFakeIp(&pIpNode->Ip);
		if (dwErrorCode != NO_ERROR) goto err_no_avail_fake_ip;
		LL_PREPEND(CurrentProcessDataEntry->Ips, pIpNode);
	
		FakeIpEntry = (tab_fake_ip_hostname_t*)HeapAlloc(GetProcessHeap(), 0, sizeof(tab_fake_ip_hostname_t));
		FakeIpEntry->Ip = pIpNode->Ip;
		FakeIpEntry->dwOptionalPid = 0;
		FakeIpEntry->Hostname = TempEntry->Hostname;
		FakeIpEntry->dwResovledIpNum = TempEntry->dwResovledIpNum;
		CopyMemory(FakeIpEntry->ResolvedIps, TempEntry->ResolvedIps, sizeof(PXCH_IP_ADDRESS) * FakeIpEntry->dwResovledIpNum);

		HASH_ADD(hh, g_tabFakeIpHostname, Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), FakeIpEntry);
	
		if (bWillMapResolvedIpToHost) {
			for (dw = 0; dw < FakeIpEntry->dwResovledIpNum; dw++) {
				pIpNode = (IpNode*)HeapAlloc(GetProcessHeap(), 0, sizeof(IpNode));
				pIpNode->Ip = TempEntry->ResolvedIps[dw];
				LL_PREPEND(CurrentProcessDataEntry->Ips, pIpNode);

				ResolvedIpEntry = (tab_fake_ip_hostname_t*)HeapAlloc(GetProcessHeap(), 0, sizeof(tab_fake_ip_hostname_t));
				ResolvedIpEntry->Ip = TempEntry->ResolvedIps[dw];
				ResolvedIpEntry->dwOptionalPid = dwPid;
				ResolvedIpEntry->Hostname = TempEntry->Hostname;
				ResolvedIpEntry->dwResovledIpNum = TempEntry->dwResovledIpNum;
				CopyMemory(ResolvedIpEntry->ResolvedIps, TempEntry->ResolvedIps, sizeof(PXCH_IP_ADDRESS) * FakeIpEntry->dwResovledIpNum);

				HASH_ADD(hh, g_tabFakeIpHostname, Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), ResolvedIpEntry);
			}
		}

		PrintTablePerProcess();
		dwReturn = NO_ERROR;
		goto after_proc;

	err_no_proc_entry:
		dwReturn = ERROR_NOT_FOUND;
		goto after_proc;

	err_no_avail_fake_ip:
		goto ret_free;

	ret_free:
		HeapFree(GetProcessHeap(), 0, pIpNode);
		dwReturn = dwErrorCode;
		goto after_proc;
	});
}

DWORD GetMsgHostnameAndResolvedIpFromMsgIp(IPC_MSGBUF chMessageBuf, PXCH_UINT32 *pcbMessageSize, const IPC_MSGHDR_IPADDRESS* pMsgIp)
{
	tab_fake_ip_hostname_t AsKey;
	tab_fake_ip_hostname_t* Entry;
	DWORD dwErrorCode;

	AsKey.Ip = pMsgIp->Ip;
	AsKey.dwOptionalPid = pMsgIp->dwPid;

	Entry = NULL;
	HASH_FIND(hh, g_tabFakeIpHostname, &AsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), Entry);

	if (Entry) {
		LOGI(L"ResolvedIp %ls -> Hostname %ls", FormatHostPortToStr(&pMsgIp->Ip, sizeof(PXCH_IP_ADDRESS)), Entry->Hostname.szValue);
		dwErrorCode = HostnameAndResolvedIpToMessage(chMessageBuf, pcbMessageSize, pMsgIp->dwPid, &Entry->Hostname, FALSE /*ignored*/, Entry->dwResovledIpNum, Entry->ResolvedIps);
		if (dwErrorCode != NO_ERROR) goto error;

		return NO_ERROR;
	}

	AsKey.dwOptionalPid = 0;
	Entry = NULL;
	HASH_FIND(hh, g_tabFakeIpHostname, &AsKey.Ip, sizeof(PXCH_IP_ADDRESS) + sizeof(PXCH_UINT32), Entry);

	if (Entry) {
		LOGI(L"FakeIp %ls -> Hostname %ls", FormatHostPortToStr(&pMsgIp->Ip, sizeof(PXCH_IP_ADDRESS)), Entry->Hostname.szValue);
		dwErrorCode = HostnameAndResolvedIpToMessage(chMessageBuf, pcbMessageSize, pMsgIp->dwPid, &Entry->Hostname, FALSE /*ignored*/, Entry->dwResovledIpNum, Entry->ResolvedIps);
		if (dwErrorCode != NO_ERROR) goto error;

		return NO_ERROR;
	}

	return ERROR_NOT_FOUND;

error:
	return dwErrorCode;
}

DWORD HandleMessage(int i, IPC_INSTANCE* pipc)
{
	PIPC_MSGBUF pMsg = pipc->chReadBuf;

	if (MsgIsType(WSTR, pMsg)) {
		WCHAR sz[IPC_BUFSIZE / sizeof(WCHAR)];
		MessageToWstr(sz, pMsg, pipc->cbRead);
		StdWprintf(STD_ERROR_HANDLE, L"%ls", sz);
		StdFlush(STD_ERROR_HANDLE);

		goto after_handling_resp_ok;
	}

	if (MsgIsType(CHILDDATA, pMsg)) {
		REPORTED_CHILD_DATA ChildData;
		LOGV(L"Message is CHILDDATA");
		MessageToChildData(&ChildData, pMsg, pipc->cbRead);
		LOGD(L"Child process pid " WPRDW L" created.", ChildData.dwPid);
		LOGV(L"RegisterNewChildProcess...");
		RegisterNewChildProcess(&ChildData);
		LOGV(L"RegisterNewChildProcess done.");
		goto after_handling_resp_ok;
	}

	if (MsgIsType(QUERYSTORAGE, pMsg)) {
		REPORTED_CHILD_DATA ChildData = { 0 };
		LOGV(L"Message is QUERYSTORAGE");
		MessageToQueryStorage(&ChildData.dwPid, pMsg, pipc->cbRead);
		QueryChildStorage(&ChildData);
		ChildDataToMessage(pipc->chWriteBuf, &pipc->cbToWrite, &ChildData);
		goto ret;
	}

	if (MsgIsType(HOSTNAMEANDRESOLVEDIP, pMsg)) {
		BOOL bWillMapResolvedIpToHost;
		PXCH_UINT32 dwPid;
		tab_fake_ip_hostname_t Entry;
		PXCH_IP_ADDRESS FakeIp = { 0 };
		
		LOGV(L"Message is HOSTNAMEANDRESOLVEDIP");
		MessageToHostnameAndResolvedIp(&dwPid, &Entry.Hostname, &bWillMapResolvedIpToHost, &Entry.dwResovledIpNum, Entry.ResolvedIps, pMsg, pipc->cbRead);
		if (RegisterHostnameAndGetFakeIp(&FakeIp, &Entry, dwPid, bWillMapResolvedIpToHost) != NO_ERROR) LOGE(L"RegisterHostnameAndGetFakeIp() failed");
		IpAddressToMessage(pipc->chWriteBuf, &pipc->cbToWrite, 0 /*ignored*/, &FakeIp);
		goto ret;
	}

	if (MsgIsType(IPADDRESS, pMsg)) {
		LOGV(L"Message is IPADDRESS");
		if (GetMsgHostnameAndResolvedIpFromMsgIp(pipc->chWriteBuf, &pipc->cbToWrite, (const IPC_MSGHDR_IPADDRESS*)pMsg) != NO_ERROR) LOGE(L"GetHostnameFromIp() failed");
		goto ret;
	}

	goto after_handling_not_recognized;

after_handling_resp_ok:
	WstrToMessage(pipc->chWriteBuf, &pipc->cbToWrite, L"OK");
	return 0;

after_handling_not_recognized:
	WstrToMessage(pipc->chWriteBuf, &pipc->cbToWrite, L"NOT RECOGNIZED");
	return 0;

ret:
	return 0;
}


DWORD InitProcessBookkeeping(void)
{
	g_tabPerProcess = NULL;
	g_tabFakeIpHostname = NULL;

	if ((g_hDataMutex = CreateMutexW(NULL, FALSE, NULL)) == NULL) return GetLastError();

	return 0;
}
