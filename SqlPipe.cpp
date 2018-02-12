// SqlPipe.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

// The following come from "SQL Server 2005 Virtual Backup Device Interface (VDI) Specification" 
// http://www.microsoft.com/downloads/en/details.aspx?familyid=416f8a51-65a3-4e8e-a4c8-adfe15e850fc
#include "vdi/vdi.h"        // interface declaration
#include "vdi/vdierror.h"   // error constants
#include "vdi/vdiguid.h"    // define the interface identifiers.
#include "SqlPipe.h"

using namespace std;

// Globals
TCHAR* _serverInstanceName;
bool _optionQuiet = false;

CString connectString = L"Provider=SQLOLEDB; Data Source=.; Initial Catalog=master; Integrated Security=SSPI;";
CString connectLogin = L"";
CString connectPassword = L"";

// printf to stdout (if -q quiet option isn't specified)
void log(const TCHAR* format, ...)
{
	if (_optionQuiet)
		return;

	// Basically do a fprintf to stderr. The va_* stuff is just to handle variable args
	va_list arglist;
	va_start(arglist, format);
	vfwprintf(stderr, format, arglist);
}

#pragma region output error messages
// printf to stdout (regardless of -q quiet option)
void err(const TCHAR* format, ...)
{
	va_list arglist;
	va_start(arglist, format);
	vfwprintf(stderr, format, arglist);
}

// Get human-readable message given a HRESULT
_bstr_t errorMessage(DWORD messageId)
{
	LPTSTR szMessage;
	if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, messageId, 0, (LPTSTR)&szMessage, 0, NULL))
	{
		szMessage = (LPTSTR)LocalAlloc(LPTR, 50 * sizeof(TCHAR));
		_snwprintf(szMessage, 50 * sizeof(TCHAR), L"Unknown error 0x%x.\n", messageId);		
	}
	_bstr_t retval = szMessage;
	LocalFree(szMessage);
	return retval;
}
#pragma endregion

#pragma region Execute SQL query
// Execute the given SQL against _serverInstanceName
DWORD executeSql(TCHAR* sql)
{
	//log(L"\nexecuteSql '%s'\n", sql);
	HRESULT hr;
	
	// Use '_Connection_Deprecated' interface for maximum MDAC compatibility
	// BTW: Windows 7 SP1 introduced _Connection_Deprecated: See:
    //   http://social.msdn.microsoft.com/Forums/en/windowsgeneraldevelopmentissues/thread/3a4ce946-effa-4f77-98a6-34f11c6b5a13
	// and 
	//   http://support.microsoft.com/kb/2517589
	_ConnectionPtr conn;
	hr = conn.CreateInstance(__uuidof(Connection));
	if (FAILED(hr))
	{
		err(L"ADODB.Connection CreateInstance failed: %s", (TCHAR*)errorMessage(hr));
		return hr;
	}

	// "lpc:..." ensures shared memory...
	_bstr_t serverName = "lpc:.";
	if (_serverInstanceName != NULL)
		serverName += "\\" + _bstr_t(_serverInstanceName);

	try
	{
		//conn->ConnectionString = "Provider=SQLOLEDB; Data Source=" + serverName + "; Initial Catalog=master; Integrated Security=SSPI;";
		//log(L"> Connect: %s\n", (TCHAR*)conn->ConnectionString);

		conn->ConnectionString = connectString.GetString();
		wcerr << "connect: " << connectString.GetString() << endl;

		conn->ConnectionTimeout = 25;

		long connectOpt = 0;
		if (connectLogin.GetLength() < 1) {
			connectOpt = adConnectUnspecified;
			conn->Open("", "", "", connectOpt);
		} else {
			wcerr << "login: " << connectLogin.GetString() << endl;
			conn->Open("", connectLogin.GetString(), connectPassword.GetString(), connectOpt);
		}

		//conn->Open("", "", "", connectOpt);
	}
	catch(_com_error e)
	{
		err(L"\nFailed to open connection to database");
		err(L"%s [%s]\n", (TCHAR*)e.Description(), (TCHAR*)e.Source());
		return e.Error();
	}

	try 
	{	
		//log(L"> SQL: %s\n", sql);
		variant_t recordsAffected; 
		conn->CommandTimeout = 0;
		conn->Execute(sql, &recordsAffected, adExecuteNoRecords);
		conn->Close();
	}
	catch(_com_error e)
	{
		err(L"\nQuery failed: '%s'\n\n%s [%s]\n", sql, (TCHAR*)e.Description(), (TCHAR*)e.Source());
		//err(L"  Errors:\n", conn->Errors->Count);
		//for (int i = 0; i < conn->Errors->Count; i++)
		//	err(L"  - %s\n\n", (TCHAR*)conn->Errors->Item[i]->Description);

		conn->Close();
		return e.Error();
	}

	return 0;
}

#pragma endregion

#pragma region Монтирование устройства и передача резервной копии
// Transfer data from virtualDevice to backupfile or vice-versa
HRESULT performTransfer(IClientVirtualDevice* virtualDevice, FILE* backupfile)
{
	VDC_Command *   cmd;
	DWORD           completionCode;
	DWORD           bytesTransferred;
	HRESULT         hr;
	DWORD           totalBytes = 0;

	while (SUCCEEDED (hr = virtualDevice->GetCommand(3 * 60 * 1000, &cmd)))
	{
		//log(L">command %d, size %d\n", cmd->commandCode, cmd->size);
		bytesTransferred = 0;

		switch (cmd->commandCode)
		{
		case VDC_Read:

			while(bytesTransferred < cmd->size)
				bytesTransferred += fread(cmd->buffer + bytesTransferred, 1, cmd->size - bytesTransferred, backupfile);

			totalBytes += bytesTransferred;
			log(L"%d bytes read                               \r", totalBytes);

			cmd->size = bytesTransferred;

			completionCode = ERROR_SUCCESS;
			break;

		case VDC_Write:
			//log(L"VDC_Write - size: %d\r\n", cmd->size);

			while(bytesTransferred < cmd->size)
				bytesTransferred += fwrite(cmd->buffer + bytesTransferred, 1, cmd->size - bytesTransferred, backupfile);

			totalBytes += bytesTransferred;

			log(L"%d bytes written\r", totalBytes);
			completionCode = ERROR_SUCCESS;
			break;

		case VDC_Flush:
			//log(L"\nVDC_Flush %d\n", cmd->size);
			completionCode = ERROR_SUCCESS;
			break;

		case VDC_ClearError:
			log(L"\nVDC_ClearError\n");
			//Debug::WriteLine("VDC_ClearError");
			completionCode = ERROR_SUCCESS;
			break;

		default:
			// If command is unknown...
			completionCode = ERROR_NOT_SUPPORTED;
		}

		hr = virtualDevice->CompleteCommand(cmd, completionCode, bytesTransferred, 0);
		if (FAILED(hr))
		{
			err(L"\nvirtualDevice->CompleteCommand failed: %s\n", (TCHAR*)errorMessage(hr));
			return hr;
		}
	}

	log(L"\n");

	if (hr != VD_E_CLOSE)
	{
		err(L"virtualDevice->GetCommand failed: ");
		if (hr == VD_E_TIMEOUT)
			err(L" timeout awaiting data.\n");
		else if (hr == VD_E_ABORT)
			err(L" transfer was aborted.\n");
		else
			err(L"%s\n", (TCHAR*)errorMessage(hr));

		return hr;
	}

	return NOERROR;
}
int mountAndTransferVirtualDevice(/*TCHAR *command, */ HRESULT &hr, TCHAR virtualDeviceName[39], const TCHAR *sql, FILE *backupFile)
{
    // Create Device Set
    IClientVirtualDeviceSet2 * virtualDeviceSet;
    hr = CoCreateInstance(
        CLSID_MSSQL_ClientVirtualDeviceSet,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_IClientVirtualDeviceSet2,
        (void**)&virtualDeviceSet);

    if (FAILED(hr))
    {
        err(L"Could not create VDI component. Check registration of SQLVDI.DLL. %s\n", (TCHAR*)errorMessage(hr));
        return 2;
    }

    // Create Device
    VDConfig vdConfig = {0};
    vdConfig.deviceCount = 1;
    hr = virtualDeviceSet->CreateEx(_serverInstanceName, virtualDeviceName, &vdConfig);
    if (!SUCCEEDED (hr))
    {
        err(L"IClientVirtualDeviceSet2.CreateEx failed\r\n");

        switch(hr)
        {
        case VD_E_INSTANCE_NAME:
            err(L"Didn't recognize the SQL Server instance name '"+ _bstr_t(_serverInstanceName) + L"'.\r\n");
            break;
        case E_ACCESSDENIED:
            err(L"Access Denied: You must be logged in as a Windows administrator to create virtual devices.\r\n");
            break;
        default:
            err(L"%s\n", (TCHAR*)errorMessage(hr));
            break;
        }
        return 3;
    }


    // Invoke backup on separate thread because virtualDeviceSet->GetConfiguration will block until "BACKUP DATABASE..."
    DWORD threadId;
    HANDLE executeSqlThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&executeSql, (LPVOID)sql, 0, &threadId);

    // Ready...
    hr = virtualDeviceSet->GetConfiguration(30000, &vdConfig);
    if (FAILED(hr))
    {
        err(L"\n%s: virtualDeviceSet->GetConfiguration failed "/*, command*/);
        if (hr == VD_E_TIMEOUT)
            err(L"Timed out waiting for backup to be initiated.\n");
        else
            err(L"%s\n", (TCHAR*)errorMessage(hr));
        return 3;
    }

    // Steady...
    IClientVirtualDevice *virtualDevice = NULL;
    hr = virtualDeviceSet->OpenDevice(virtualDeviceName, &virtualDevice);
    if (FAILED(hr))
    {
        err(L"virtualDeviceSet->OpenDevice failed: 0x%x - ");
        if (hr == VD_E_TIMEOUT)
            err(L" timeout.\n");
        else
            err(L" %s.\n", (TCHAR*)errorMessage(hr));
        return 4;
    }

    // Go
    _setmode(_fileno(backupFile), _O_BINARY); //ensure \n's in STDOUT don't get tampered with
    hr = performTransfer(virtualDevice, backupFile);

    WaitForSingleObject(executeSqlThread, 5000);

    // Tidy up
    CloseHandle(executeSqlThread);
    virtualDeviceSet->Close();
    virtualDevice->Release();
    virtualDeviceSet->Release();
    return 0;
}
#pragma endregion

#pragma region Расширяем CString c++ для iostream
std::wostream& operator << (std::wostream& os, const CString& str)
{
	if (str.GetLength() > 0)  //GetLength???
	{
		os << CStringA(str).GetString();
	}
	return os;
}
std::ostream& operator << (std::ostream& os, const CString& str)
{
	if (str.GetLength() > 0)  //GetLength???
	{
		os << CStringA(str).GetString();
	}
	return os;
}
#pragma endregion

#define ACTION_BACKUP  1
#define ACTION_RESTORE 2

// Entry point
int _tmain(int argc, _TCHAR* argv[])
{
	#pragma region Тестирование CString
	/*
	CString aCString = CString(_T("A string"));
	//log(aCString);
	std::wcout << aCString.GetString() << std::endl;
	std::wcout << aCString << std::endl;

	CString tmpl = CString(_T("Replace ${var1} to ${var2}"));
	tmpl.Replace(_T("${var1}"), _T("val 1"));
	tmpl.Replace(_T("${var2}"), _T("val b"));
	std::wcout << tmpl << std::endl;

	CString tmpEnvVar = CString();
	//tmpEnvVar.GetEnvironmentVariable(_T("TEMP"));
	//std::wcout << L"TEMP=" << (tmpEnvVar) << std::endl;

	system("PAUSE");

	return 0;
	*/
	#pragma endregion

	boolean sqlCommandSet = false;
	boolean connStringSet = false;

	CString sql = CString(_T(""));

	int action = -1;

	int parseState = 0;
	for (int argi = 0; argi < argc; argi++)
	{
		CString arg = CString(argv[argi]);
		switch (parseState) {
		case 0:
			if (arg == L"backup")   { action = ACTION_BACKUP; }
			if (arg == L"restore")  { action = ACTION_RESTORE; }
			if (arg == L"-sql"   )  { parseState = 1; }
			if (arg == L"-sqlenv")  { parseState = 2; }
			if (arg == L"-conn"  )  { parseState = 3; }
			if (arg == L"-connenv") { parseState = 4; }
			if (arg == L"-login")   { parseState = 5; }
			if (arg == L"-loginenv"){ parseState = 6; }
			if (arg == L"-passwd")  { parseState = 7; }
			if (arg == L"-passwdenv") { parseState = 8; }
			break;
		case 1:
			sql = CString(argv[argi]);
			sqlCommandSet = true;
			parseState = 0;
			break;
		case 2:
			sql = CString();
			sql.GetEnvironmentVariable(argv[argi]);
			sqlCommandSet = true;
			parseState = 0;
			break;
		case 3:
			connectString = CString(argv[argi]);
			parseState = 0;
			connStringSet = true;
			break;
		case 4:
			connectString = CString();
			connectString.GetEnvironmentVariable(argv[argi]);
			parseState = 0;
			connStringSet = true;
			break;
		case 5:
			connectLogin = CString(argv[argi]);
			parseState = 0;
			break;
		case 6:
			connectLogin = CString();
			connectLogin.GetEnvironmentVariable(argv[argi]);
			parseState = 0;
			break;
		case 7:
			connectPassword = CString(argv[argi]);
			parseState = 0;
			break;
		case 8:
			connectPassword = CString();
			connectPassword.GetEnvironmentVariable(argv[argi]);
			parseState = 0;
			break;
		}
	}

	if (!sqlCommandSet) {
		wcerr << L"sql command not set, use command line arg: -sql sql_command" << endl;
		return 1;
	}

	if (!connStringSet) {
		wcerr << L"sql connect string not set, use command line arg: -conn sql_connect_string" << endl;
		return 1;
	}

	if (action <= 0) {
		wcerr << L"action not set, use line arg: backup | restore" << endl;
		return 1;
	}

	wcerr << L"passed sql command: " << sql << endl;

	///////////////////////// INIT COM/ACTIVEX //////////////////////////////////
	// wcerr << L"action: init COM/ActiveX" << endl;

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		// Switching from apartment to multi-threaded is OK
		if (hr != RPC_E_CHANGED_MODE)
		{
			err(L"CoInitializeEx failed: ", hr);
			return 1;
		}
	}

	if (action == ACTION_BACKUP) {
		wcerr << L"action: backup" << endl;

		TCHAR virtualDeviceName[39];
		GUID guid; CoCreateGuid(&guid);
		StringFromGUID2(guid, virtualDeviceName, sizeof(virtualDeviceName));
		wcerr << "created guid: " << virtualDeviceName << endl;

		sql.Replace(_T("${guid}"), virtualDeviceName);
		wcerr << "prepared sql: " << sql << endl;

		FILE* backupFile = NULL;
		backupFile = stdout;
		wcerr << "target: " << "stdout" << endl;

		const TCHAR* tsql;
		LPCWSTR lpwstr = sql.GetString();
		tsql = lpwstr;
		hr = mountAndTransferVirtualDevice(hr, virtualDeviceName, tsql, backupFile);

		return hr;
	}

	if (action == ACTION_RESTORE) {
		wcerr << L"action: restore" << endl;

		TCHAR virtualDeviceName[39];
		GUID guid; CoCreateGuid(&guid);
		StringFromGUID2(guid, virtualDeviceName, sizeof(virtualDeviceName));
		wcerr << "created guid: " << virtualDeviceName << endl;

		sql.Replace(_T("${guid}"), virtualDeviceName);
		wcerr << "prepared sql: " << sql << endl;

		FILE* backupFile = NULL;
		backupFile = stdin;
		wcerr << "target: " << "stdin" << endl;

		const TCHAR* tsql;
		LPCWSTR lpwstr = sql.GetString();
		tsql = lpwstr;
		hr = mountAndTransferVirtualDevice(hr, virtualDeviceName, tsql, backupFile);

		return hr;
	}

	wcerr << "undefined action" << endl;
	return 1;

	/****
	if (argc < 3)
	{
		err(L"\n\
Action and database required.\n\
\n\
Usage: sqlpipe backup|restore <database> [options]\n\
Options:\n\
  -q Quiet, don't print messages to STDERR\n\
  -i \"instancename\"\n\
  -f \"file\" Write/read file instead of STDOUT/STDIN\n");
		return 1;
	}

	TCHAR* command = _wcslwr(argv[1]);
	TCHAR* databaseName = argv[2];

	TCHAR* filePath = NULL;

	// Parse options
	for (int i = 0; i < argc; i++)
	{
		TCHAR* arg = _wcslwr(_wcsdup(argv[i]));

		if (wcscmp(arg, L"-q") == 0)
			_optionQuiet = true;

		if (wcscmp(arg, L"-i") == 0)
			_serverInstanceName = argv[i+1];

		if (wcscmp(arg, L"-f") == 0)
			filePath = argv[i+1];

		free(arg);
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		// Switching from apartment to multi-threaded is OK
		if(hr != RPC_E_CHANGED_MODE)
		{
			err(L"CoInitializeEx failed: ", hr);
			return 1;
		}
	}

    // Generate virtualDeviceName
	TCHAR virtualDeviceName[39];
	GUID guid; CoCreateGuid(&guid);	
	StringFromGUID2(guid, virtualDeviceName, sizeof(virtualDeviceName));

	TCHAR* sql;
	FILE* backupFile = NULL;
	if (wcscmp(command, L"backup") == 0)
	{
		sql = "BACKUP DATABASE [" + _bstr_t(databaseName) + "] TO VIRTUAL_DEVICE = '" + virtualDeviceName + "'";
		if (filePath == NULL)
			backupFile = stdout;
		else
		{
			backupFile = _wfopen(filePath, L"w");
			if (backupFile == NULL)
			{
				err(L"Error creating '%s': %s\n", filePath, _wcserror(errno));
				return errno;
			}
		}
	}
	else if(wcscmp(command, L"restore") == 0)
	{
		hr = executeSql("CREATE DATABASE ["+ _bstr_t(databaseName) +"]");
		if (FAILED(hr))
			return hr;

        _RecordsetPtr fileList = executeRecordset("RESTORE FILELISTONLY FROM VIRTUAL_DEVICE = '" + _bstr_t(virtualDeviceName) + "'");
        _bstr_t restoreSql("RESTORE DATABASE [" + _bstr_t(databaseName) + "] FROM VIRTUAL_DEVICE = '" + virtualDeviceName + "' WITH");
        while(!fileList->EndOfFile)
        {
            _bstr_t fileType(fileList->Fields->GetItem("Type")->Value);
            _bstr_t logicalName = fileList->Fields->GetItem("LogicalName")->Value;
            restoreSql += " MOVE '" + logicalName + "' TO 'C:\\temp\\foo\\" +  logicalName + ".xdf'";
            fileList->MoveNext();
        }
        err(restoreSql);

        hr = mountAndTransferVirtualDevice(command, hr, virtualDeviceName, sql, backupFile);

		sql = "RESTORE DATABASE [" + _bstr_t(databaseName) + "] FROM VIRTUAL_DEVICE = '" + virtualDeviceName + "' WITH REPLACE";
		if (filePath == NULL)
			backupFile = stdin;
		else
		{
			backupFile = _wfopen(filePath, L"r");
			if (backupFile == NULL)
			{
				err(L"Error opening '%s': %s\n", filePath, _wcserror(errno));
				return errno;
			}
		}
	}
	else 
	{
		err(L"Unsupported command '%s': only BACKUP or RESTORE are supported.\n", command);
		return 1;
	}

    hr = mountAndTransferVirtualDevice(command, hr, virtualDeviceName, sql, backupFile);

	//log(L"%s: Finished.\n", command);
	return hr;
	*/
}
