// V2TechButton.cpp
//
// Мод для Victoria 2: кнопки, запускающие решения, скрытие этих
// решений из окна политики, процентное изменение цен, байтовые
// правки подкреплений и целей войны, текстовый индикатор.
//
// Собирать: Visual Studio, Dynamic-Link Library, платформа x86 (Win32),
// конфигурация Release, рантайм /MT.
//
// Загружается как прокси lua51.dll: оригинал переименовывается в
// lua51_real.dll, экспорты пересылаются туда через lua51_exports.h.

#ifdef _WIN64
#error This DLL must be built for Win32 (x86)
#endif

#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <wchar.h>
#include <intrin.h>
#include <ctype.h>
#include <float.h>
#include <xmmintrin.h>
#include <pmmintrin.h>

#include "lua51_exports.h"

// ---------------------------------------------------------------
// Настройки сборки
// ---------------------------------------------------------------

// Версия. Игра не сверяет бинарники, поэтому единственная защита от
// "у кого-то старая DLL" — сравнить эту строку в логах перед сетевой
// игрой.
// CLAUDE МЕНЯЙ ВЕРСИЮ ПРИ КАЖДОЙ ПРАВКЕ ФАЙЛА
#define MOD_VERSION "3.10"

// Настройки ниже читаются из v2dll_settings.ini рядом с exe при
// каждом запуске игры. Если файла ещё нет, он создаётся со
// значениями по умолчанию (перечисленными здесь). Правка файла не
// требует пересборки DLL - изменения применяются при следующем
// запуске игры.
struct Settings
{
    bool log            = true;  // лог в Logs\v2dll.log (много записей на тик, для раздачи ставить 0)
    bool buttons        = true;   // кнопки, запускающие решения
    bool decisionFilter = true;   // скрытие решений из окна политики
    bool priceDelta     = true;  // процентный шаг изменения цен
    bool popDisplay     = false;  // общее население в верхней панели (откачено: не смогли дописать сырое число в скобках без риска)
    bool versionLabel   = true;   // версия мода в подписи главного меню

    // Байтовые патчи exe из таблицы EXE_PATCHES (см. ниже по файлу)
    // управляются напрямую через BytePatch::enabled по ключам
    // PATCH_<ИМЯ> в ini - здесь только патчи exe, не входящие в эту
    // таблицу (каждый - отдельная функция со своим хуком).
    bool patchOccupiedReinforceSplit = true;
    bool patchAllyOwnerCheck         = true;
    bool patchCivilizeNullCheck      = true;
    // UI: пропуск fallback-строки, если у объекта нулевой vtable
    // (чтение [vtable+0x20] по 0 → ACCESS_VIOLATION на rva 0x4A8E0B
    // после закрытия OOS-диалога). Не трогает симуляцию / checksum.
    bool patchNullVtableUi           = true;
    bool patchGraphPointClamp        = false;
    bool patchFactoryDumpScan        = false;
    bool patchProdListVisibility     = true;
    bool patchProdTypeGate           = true;

    // Взаимоисключающе с priceDelta (ENABLE_PRICE_DELTA) - оба
    // патчат один и тот же адрес. Если включены оба, побеждает этот.
    bool patchExponentialPriceDelta = false;

    // Если true - PATCH_PROD_TYPE_GATE разрешает строить в колонии
    // ЛЮБОЙ тип производства. Если false - только типы из белого
    // списка (limit_by_local_supply=yes + PROD_TYPE_GATE_EXTRA_WHITELIST
    // ниже, см. g_extraWhitelistNames).
    bool prodTypeGateAllowAll = false;

    // Разброс броска в бою. Работает только если exe уже несёт
    // "пещеру" от стороннего Vic2_Roll_Changer.py (см. комментарий
    // у InstallCombatRoll ниже по файлу) - без неё сигнатура не
    // совпадёт и патч тихо пропустится.
    bool patchCombatRoll = true;
    int  combatRollMin   = 0;   // минимум броска
    int  combatRollMax   = 4;   // максимум броска

    // Временная диагностика бага с чек-суммой (меняется при входе в
    // партию) - логирует в v2dll.log каждое обращение к источнику
    // чек-суммы. См. InstallChecksumDiagnostic ниже по файлу.
    bool patchChecksumDiagnostic = false;

    // Отдельный Logs\v2dll_oos.log: пишется при вызове FUN_00682EC0
    // (диалог OOS_TITLE / "Games out of synch"). Не каждый тик.
    bool enableOosLog = true;

    // Logs\v2dll_crash.log + Logs\v2dll_crash_*.dmp при необработанном
    // исключении / abort. Не каждый тик. Каждый дамп — отдельный файл.
    bool enableCrashLog = true;

    // Стабильность симуляции / память / существующий TBB-пул игры.
    // Все клиенты MP обязаны иметь одну DLL, поэтому эти правки
    // считаются частью протокола, а не опциональным ускорением.
    bool patchFpuFortress      = true;  // PC=53, near, FTZ/DAZ + пин на главном цикле
    bool patchD3dFpuPreserve   = true;  // D3DCREATE_FPU_PRESERVE, чтобы D3D не сбивал CW
    bool patchHeapLfh          = true;  // Low Fragmentation Heap на кучах процесса
    bool patchThreadFpuPin     = true;  // PinFpu на старте потоков exe/tbb
    int  engineWorkerThreads   = 8;     // потолок TBB; одинаков у всех MP; 0 = не трогать

    // После дневного прохода POP (FUN_00485E40) округляем int64/2^15
    // поля денег/нужд, чтобы младшие биты не разъезжались между клиентами.
    bool patchPopQuantize      = true;
    int  popQuantizeKeepBits   = 12;    // из 15 дробных; 12 = шаг 8 единиц 2^-15

    // MP: не-хост сидит в message pump со Sleep(40) → потолок ~25 FPS
    // (даже в лобби). Sleep(0) даёт фризы скролла UI — держим 1 мс.
    bool patchMpClientSleep    = true;
    int  mpClientSleepMs       = 1;

    // Хост/SP главный цикл: Sleep(0) или Sleep(100) по флагу графики.
    // Оба плеча ставим в MAIN_LOOP_SLEEP_MS (1 = yield, без спина).
    bool patchMainLoopSleep0   = true;
    int  mainLoopSleepMs       = 1;
    bool patchD3dNoVsync       = false; // IMMEDIATE рвёт кадр при скролле списков
    bool patchHighPriority     = true;  // ABOVE_NORMAL + без power throttling
};

static Settings g_settings;

// Загрузка/сохранение настроек (v2dll_settings.ini) реализовано
// ниже по файлу, после таблицы EXE_PATCHES - подстановка значений
// по ключам PATCH_<ИМЯ> ищет патч в этой таблице по имени.
static void LoadSettings();

static bool g_logStarted = false;
static bool g_oosLogStarted = false;
static CRITICAL_SECTION g_logCs;
static bool g_logCsInit = false;

static HMODULE g_selfModule = 0;
static wchar_t g_logsDir[MAX_PATH];
static wchar_t g_logFile[MAX_PATH];
static wchar_t g_oosLogFile[MAX_PATH];
static wchar_t g_crashLogFile[MAX_PATH];
static wchar_t g_crashDumpFile[MAX_PATH];
static bool g_logDirReady = false;

static void InitLogDir()
{
    if (g_logDirReady)
        return;

    wchar_t exe[MAX_PATH];
    exe[0] = 0;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t* slash = (n && n < MAX_PATH) ? wcsrchr(exe, L'\\') : 0;
    if (slash)
    {
        *slash = 0;
        swprintf_s(g_logsDir, L"%s\\Logs", exe);
    }
    else
    {
        wcscpy_s(g_logsDir, L"Logs");
    }

    if (!CreateDirectoryW(g_logsDir, NULL))
    {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
            wcscpy_s(g_logsDir, L"Logs");
        CreateDirectoryW(g_logsDir, NULL);
    }

    swprintf_s(g_logFile, L"%s\\v2dll.log", g_logsDir);
    swprintf_s(g_oosLogFile, L"%s\\v2dll_oos.log", g_logsDir);
    swprintf_s(g_crashLogFile, L"%s\\v2dll_crash.log", g_logsDir);
    swprintf_s(g_crashDumpFile, L"%s\\v2dll_crash.dmp", g_logsDir);
    g_logDirReady = true;
}

static void Log(const char* fmt, ...)
{
    if (!g_settings.log)
        return;

    InitLogDir();

    if (g_logCsInit)
        EnterCriticalSection(&g_logCs);

    FILE* f = 0;
    if (_wfopen_s(&f, g_logFile, g_logStarted ? L"a" : L"w") != 0 || !f)
    {
        if (g_logCsInit)
            LeaveCriticalSection(&g_logCs);
        return;
    }

    g_logStarted = true;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fprintf(f, "\n");
    fclose(f);

    if (g_logCsInit)
        LeaveCriticalSection(&g_logCs);
}


// ---------------------------------------------------------------
// НАСТРОЙКА СОДЕРЖИМОГО — правится здесь
// ---------------------------------------------------------------

// Вид — экран игры со своим классом. Чтобы подключить новый, нужны:
//   rvaVtable   — адрес vftable класса минус 0x400000
//   slot        — слот, который вызывается регулярно, пока окно живо
//   tooltipSlot — true, если это слот подсказки (другая сигнатура)
//   offGlue     — смещение любой CButtonObserverGlue внутри вида
//   offContainer— смещение GUI-контейнера, у всех виденных видов 0x4C
//   window      — вложенное окно из .gui, либо 0
//
// Слот подбирается опытом: у технологий сработал 11 (Update),
// у бюджета и производства — 10 (подсказка, срабатывает на наведении).
struct ViewDef
{
    const char* name;
    DWORD       rvaVtable;
    int         slot;
    bool        tooltipSlot;
    int         offGlue;
    int         offContainer;
    const char* window;
};

static const ViewDef VIEWS[] =
{
    { "CTechnologyView", 0xA17FA4, 11, false, 0x60, 0x4C, "selected_tech_window" },
    { "CBudgetView",     0xA059F0, 10, true,  0x5C, 0x4C, 0                      },
    { "CProductionView", 0xA0FECC, 10, true,  0x120, 0x4C, 0                     },
    { "CPoliticsView",   0xA0E458, 10, true,  0x00, 0x4C, 0                      },
};

static const int VIEW_POLITICS = 3;

// Кнопки. view — номер строки в VIEWS, начиная с нуля.
struct ButtonDef
{
    int         view;
    const char* button;      // имя элемента из .gui
    const char* decision;    // имя решения из decisions/*.txt
};

static const ButtonDef BUTTONS[] =
{
    { 0, "FE_ACADEMIES_BDSM",     "open_academy_decisions_dec" },
    { 0, "FE_RPROJECTS_BDSM",     "open_research_projects_dec" },
    { 1, "FE_BUDGET_DIPLO_BDSM",  "exchange_settings_dec"      },
};

static const int VIEW_COUNT = sizeof(VIEWS) / sizeof(VIEWS[0]);
static const int BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);

// Потолки: под каждый вид и каждую кнопку нужен свой переходник,
// а их приходится объявлять заранее — см. макросы ниже.
static const int MAX_VIEWS = 4;
static const int MAX_BUTTONS = 8;


// ---------------------------------------------------------------
// Адреса. RVA = адрес в Ghidra минус 0x400000.
// ASLR включён, поэтому всё считается от базы модуля в рантайме.
// ---------------------------------------------------------------

static const DWORD RVA_ONMAKEDECISION = 0x2DCE10;  // OnMakeDecisionClicked
static const DWORD RVA_VTABLE_DECISION = 0xA29B54;  // CDecision::vftable

static const int VT_SLOT_ISVALID = 6;   // CDecision, смещение 0x18

// Границы PoliticsView_OnClick — функции, строящей список решений.
static const DWORD RVA_POLITICS_DRAW_BEGIN = 0x2DB2E0;
static const DWORD RVA_POLITICS_DRAW_END = 0x2DC750;

// ---------------------------------------------------------------
// Смещения внутри объектов интерфейса
// ---------------------------------------------------------------

static const int GLUE_SIZE = 44;    // размер склейки
static const int OFF_GLUE_METHOD = 0x08;  // склейка -> указатель на метод

static const int VT_FIND_WINDOW = 0x6C;  // контейнер: найти вложенное окно
static const int VT_FIND_CHILD = 0x34;  // окно: найти кнопку по имени
static const int OFF_OBSERVABLE = 0x54;  // кнопка -> Observable
static const int VT_ADD_OBSERVER = 0x04;  // Observable: AddObserver
static const int VT_GET_NAME = 0x44;  // элемент: имя из .gui

// Поддельный элемент для OnMakeDecisionClicked: функция читает у него
// только +0x14 (данные std::string) и +0x28 (_Myres).
static const int ELEM_STRDATA = 0x14;
static const int ELEM_STRRES = 0x28;


// ---------------------------------------------------------------
// std::string движка: буфер 16 байт, _Mysize на +0x10, _Myres на +0x14
// ---------------------------------------------------------------

struct GStr
{
    char     data[16];
    unsigned size;
    unsigned res;
};

static void MakeStr(GStr* s, char* storage, unsigned storageSize, const char* text)
{
    size_t n = strlen(text);

    memset(s, 0, sizeof(GStr));
    s->size = (unsigned)n;

    if (n < 16)
    {
        memcpy(s->data, text, n + 1);
        s->res = 15;
    }
    else
    {
        memcpy(storage, text, n + 1);
        *(char**)s->data = storage;
        s->res = storageSize - 1;
    }
}


// ---------------------------------------------------------------
// Вызов виртуальных методов с соглашением __thiscall.
// MSVC не даёт объявить __thiscall-указатель напрямую, поэтому
// используем __fastcall с фиктивным EDX — раскладка совпадает.
// ---------------------------------------------------------------

typedef void* (__fastcall* tCallNoArgs)(void* ecx, void* edx);
typedef void* (__fastcall* tCallOneArg)(void* ecx, void* edx, void* arg);

static void* VCall0(void* obj, int byteSlot)
{
    void** vt = *(void***)obj;
    tCallNoArgs fn = (tCallNoArgs)vt[byteSlot / 4];
    return fn(obj, 0);
}

static void* VCall1(void* obj, int byteSlot, void* arg)
{
    void** vt = *(void***)obj;
    tCallOneArg fn = (tCallOneArg)vt[byteSlot / 4];
    return fn(obj, 0, arg);
}


// Имя класса объекта через RTTI (MSVC x86): перед таблицей виртуальных
// функций лежит указатель на CompleteObjectLocator, у него 4-е поле —
// указатель на TypeDescriptor, а в нём с +8 лежит "исковерканное" имя
// вида ".?AVCProvinceView@@". Не работает для классов без RTTI/vtable.
static const char* GetRTTIClassName(void* obj)
{
    if (!obj)
        return "(null)";

    void** vtable = *(void***)obj;
    if (!vtable)
        return "(no vtable)";

    DWORD* completeObjectLocator = *(DWORD**)((unsigned char*)vtable - 4);
    if (!completeObjectLocator)
        return "(no RTTI)";

    DWORD* typeDescriptor = (DWORD*)completeObjectLocator[3];
    if (!typeDescriptor)
        return "(no RTTI)";

    return (const char*)typeDescriptor + 8;
}


// Содержимое std::string движка.
static const char* GStrText(void* str)
{
    if (!str)
        return "";

    unsigned char* p = (unsigned char*)str;
    unsigned res = *(unsigned*)(p + 0x14);

    const char* data = (res > 15) ? *(const char**)p : (const char*)p;
    return data ? data : "";
}


// Записать текст в существующую std::string, не превышая ёмкости.
static void GStrSet(void* str, const char* text)
{
    if (!str)
        return;

    unsigned char* p = (unsigned char*)str;
    unsigned res = *(unsigned*)(p + 0x14);
    unsigned len = (unsigned)strlen(text);

    if (len > res)
        len = res;

    char* data = (res > 15) ? *(char**)p : (char*)p;
    if (!data)
        return;

    memcpy(data, text, len);
    data[len] = 0;
    *(unsigned*)(p + 0x10) = len;
}


// ---------------------------------------------------------------
// Состояние
// ---------------------------------------------------------------

static DWORD  g_base = 0;
static DWORD  g_imageSize = 0;
static void* g_fnOnMakeDecision = 0;

typedef BOOL(WINAPI* tIsBadReadPtr)(const void*, UINT_PTR);
static tIsBadReadPtr g_fnIsBadReadPtr = 0;

// Замена запрещённого IsBadReadPtr: та же сигнатура (TRUE = память
// плохая), но через SEH, без обхода PAGE_GUARD ядром.
static BOOL WINAPI SafeIsBadReadPtr(const void* lp, UINT_PTR ucb)
{
    if (!lp || ucb == 0)
        return TRUE;
    __try
    {
        volatile const unsigned char* p = (const unsigned char*)lp;
        (void)p[0];
        if (ucb > 1)
            (void)p[ucb - 1];
        return FALSE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return TRUE;
    }
}

static unsigned char g_glue[MAX_BUTTONS][GLUE_SIZE];
static unsigned char g_fakeElem[MAX_BUTTONS][0x30];
static char          g_decisionText[MAX_BUTTONS][128];
static char          g_nameStorage[128];


// ---------------------------------------------------------------
// Обработчики кликов
//
// __stdcall: очистку стека делает сама функция. При __cdecl компилятор
// добавлял свой add esp,4 поверх её ret 4, и возврат уходил по битому
// стеку — проверено логом.
// ---------------------------------------------------------------

typedef void(__stdcall* tOnMakeDecision)(void*);

static void __cdecl FireDecision(int index)
{
    if (!g_fnOnMakeDecision || index < 0 || index >= BUTTON_COUNT)
    {
        Log("FireDecision: bad state, index=%d", index);
        return;
    }

    Log("FireDecision: '%s' -> '%s'",
        BUTTONS[index].button, BUTTONS[index].decision);

    ((tOnMakeDecision)g_fnOnMakeDecision)(g_fakeElem[index]);
}


// Склейка хранит один указатель на метод, поэтому у каждой кнопки
// должен быть свой обработчик. Различаются они только номером.
//
// Форма с __asm перед каждой инструкцией: блочная запись __asm { ... }
// внутри макроса схлопывается в одну строку и не разбирается.
#define THUNK(n)                             \
    __declspec(naked) static void Thunk##n() \
    {                                        \
        __asm push ebp                       \
        __asm mov  ebp, esp                  \
        __asm pushad                         \
        __asm push n                         \
        __asm call FireDecision              \
        __asm add  esp, 4                    \
        __asm popad                          \
        __asm mov  esp, ebp                  \
        __asm pop  ebp                       \
        __asm ret                            \
    }

THUNK(0) THUNK(1) THUNK(2) THUNK(3)
THUNK(4) THUNK(5) THUNK(6) THUNK(7)

static void* const THUNKS[MAX_BUTTONS] =
{
    (void*)&Thunk0, (void*)&Thunk1, (void*)&Thunk2, (void*)&Thunk3,
    (void*)&Thunk4, (void*)&Thunk5, (void*)&Thunk6, (void*)&Thunk7,
};


// Строка тултипа "PLURALITY_CHANGE" в игре хардкожена в exe и всегда
// добавляется как "<локализация>: <число>" — вырезать её сборку внутри
// движка рискованно (рядом идут байты состояния раскрутки стека C++
// исключений для временных std::string). Вместо этого текст локализации
// уже очищен (localisation/*.csv), и от строки остаётся голое число
// вида "0.00" на отдельной строке. Убираем такие строки уже из готового
// текста тултипа, после того как оригинал его построил.
static bool IsBareNumberLine(const char* s, size_t len)
{
    // Пустая метка в локализации хранится как один пробел (иначе игра
    // подставляет вместо неё сырой ключ), поэтому строка выглядит как
    // " : 0.00" — пропускаем ведущие пробелы/табы/двоеточие тоже.
    while (len && (s[0] == ' ' || s[0] == '\t' || s[0] == ':'))
    {
        ++s;
        --len;
    }
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r'))
        --len;

    if (len == 0)
        return false;

    bool sawDigit = false;
    size_t i = 0;
    while (i < len)
    {
        unsigned char c = (unsigned char)s[i];

        // Цветовой код Paradox: 0xA7 + один байт (например §G...§!) —
        // невидим на экране, пропускаем как есть.
        if (c == 0xA7 && i + 1 < len)
        {
            i += 2;
            continue;
        }

        if (c >= '0' && c <= '9')
        {
            sawDigit = true;
            ++i;
            continue;
        }
        if (c == '.' || c == '-' || c == '+')
        {
            ++i;
            continue;
        }
        return false;
    }
    return sawDigit;
}

static void StripBareNumberLines(void* retBuf)
{
    const char* text = GStrText(retBuf);
    if (!text || !*text)
        return;

    char buf[1024];
    size_t n = strlen(text);
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    memcpy(buf, text, n);
    buf[n] = 0;

    char out[1024];
    size_t o = 0;
    size_t lineStart = 0;

    for (size_t i = 0; i <= n; ++i)
    {
        if (i == n || buf[i] == '\n')
        {
            size_t lineLen = i - lineStart;
            if (!IsBareNumberLine(buf + lineStart, lineLen))
            {
                memcpy(out + o, buf + lineStart, lineLen);
                o += lineLen;
                if (i < n)
                    out[o++] = '\n';
            }
            lineStart = i + 1;
        }
    }
    out[o] = 0;

    GStrSet(retBuf, out);
}


// Подсказка над элементом. Вызывается после оригинала: тот кладёт
// свой текст в retBuf.
static void OnTooltip(int viewIndex, void* retBuf, void* element)
{
    if (!retBuf || !element)
        return;

    const char* name = GStrText(VCall0(element, VT_GET_NAME));

    if (viewIndex == VIEW_POLITICS && strcmp(name, "plurality") == 0)
        StripBareNumberLines(retBuf);
}


// ---------------------------------------------------------------
// Подписка кнопок
// ---------------------------------------------------------------

static bool SetupButtons(int viewIndex, void* view)
{
    const ViewDef& vd = VIEWS[viewIndex];
    unsigned char* v = (unsigned char*)view;

    void* container = *(void**)(v + vd.offContainer);
    if (!container)
    {
        Log("Setup[%s]: контейнер пуст", vd.name);
        return false;
    }

    void* host = container;

    if (vd.window)
    {
        GStr sWindow;
        MakeStr(&sWindow, g_nameStorage, sizeof(g_nameStorage), vd.window);

        host = VCall1(container, VT_FIND_WINDOW, &sWindow);
        if (!host)
        {
            Log("Setup[%s]: окно '%s' не найдено", vd.name, vd.window);
            return false;
        }
    }

    int done = 0;

    for (int i = 0; i < BUTTON_COUNT && i < MAX_BUTTONS; ++i)
    {
        if (BUTTONS[i].view != viewIndex)
            continue;

        GStr sButton;
        MakeStr(&sButton, g_nameStorage, sizeof(g_nameStorage), BUTTONS[i].button);

        void* button = VCall1(host, VT_FIND_CHILD, &sButton);
        if (!button)
        {
            Log("Setup[%s]: кнопка '%s' не найдена", vd.name, BUTTONS[i].button);
            continue;
        }

        // У каждой кнопки свой клон склейки со своим обработчиком.
        memcpy(g_glue[i], v + vd.offGlue, GLUE_SIZE);
        *(void**)(g_glue[i] + OFF_GLUE_METHOD) = THUNKS[i];

        void* observable = (unsigned char*)button + OFF_OBSERVABLE;
        VCall1(observable, VT_ADD_OBSERVER, g_glue[i]);

        Log("Setup[%s]: '%s' подписана", vd.name, BUTTONS[i].button);
        ++done;
    }

    return done > 0;
}


// ---------------------------------------------------------------
// Кнопка "Скрыть колонии" в окне производства (вкладка "Фабрики").
//
// Своя, отдельная от BUTTONS[]/THUNKS[] подписка: та система жмёт
// на MakeDecision, а тут нужен обычный флаг + перерисовка списка.
// Склейка берётся из offGlue (для CProductionView это 0x120 —
// найдено в конструкторе FUN_006ee930: param_1[0x48] — единственный
// там CButtonObserverGlue<CProductionView>, чей колбэк — сам
// FUN_006f83b0, обработчик кликов по факторийным кнопкам), но
// клонируется и патчится вручную, в стороне от g_glue[]/THUNKS[],
// чтобы не задевать существующую систему решений.
//
// FUN_006f3e70 (обновление списка вкладки "Фабрики") читает "this"
// не из ECX/стека, а из EDI, оставшегося от вызвавшей её функции
// (FUN_006f83b0, обработчик кликов по кнопкам этой вкладки) — поэтому
// вызываем её сами, выставив EDI вручную.
// ---------------------------------------------------------------

static bool          g_hideColonialStates = false;
static void*         g_hideColonialView = 0;
static void*         g_hideColonialConfiguredView = 0;
static unsigned char g_hideColonialGlue[GLUE_SIZE];

static const DWORD RVA_PRODUCTION_REFRESH_FACTORIES = 0x2F3E70;  // FUN_006f3e70

static void __cdecl OnHideColonialClicked()
{
    g_hideColonialStates = !g_hideColonialStates;
    Log("HideColonialStates: %s", g_hideColonialStates ? "включено" : "выключено");

    if (!g_hideColonialView)
        return;

    void* view = g_hideColonialView;
    void* fn = (void*)(g_base + RVA_PRODUCTION_REFRESH_FACTORIES);

    __asm {
        pushad
        mov edi, view
        call fn
        popad
    }
}

__declspec(naked) static void HideColonialThunk()
{
    __asm {
        push ebp
        mov ebp, esp
        pushad
        call OnHideColonialClicked
        popad
        mov esp, ebp
        pop ebp
        ret
    }
}

static bool SetupHideColonialButton(void* view)
{
    unsigned char* v = (unsigned char*)view;
    const ViewDef& vd = VIEWS[2];  // CProductionView

    void* container = *(void**)(v + vd.offContainer);
    if (!container)
        return false;

    GStr sWindow;
    MakeStr(&sWindow, g_nameStorage, sizeof(g_nameStorage), "factory_buttons");

    void* host = VCall1(container, VT_FIND_WINDOW, &sWindow);
    if (!host)
    {
        Log("SetupHideColonialButton: окно 'factory_buttons' не найдено");
        return false;
    }

    GStr sButton;
    MakeStr(&sButton, g_nameStorage, sizeof(g_nameStorage), "hide_colonial_states");

    void* button = VCall1(host, VT_FIND_CHILD, &sButton);
    if (!button)
    {
        Log("SetupHideColonialButton: кнопка 'hide_colonial_states' не найдена");
        return false;
    }

    memcpy(g_hideColonialGlue, v + vd.offGlue, GLUE_SIZE);
    *(void**)(g_hideColonialGlue + OFF_GLUE_METHOD) = HideColonialThunk;

    void* observable = (unsigned char*)button + OFF_OBSERVABLE;
    VCall1(observable, VT_ADD_OBSERVER, g_hideColonialGlue);

    g_hideColonialView = view;

    Log("SetupHideColonialButton: подписана");
    return true;
}


// ---------------------------------------------------------------
// Видимость строк в списке вкладки "Фабрики" (FUN_006f3e70).
//
// Развилка (абс. 0x6F424B, RVA 0x2F424B):
//   CMP dword ptr[ECX+0x84],0 ; JLE +8   (ECX = указатель на state,
//   уже загружен вызывающим кодом чуть раньше)
//   CMP ESI,EBX ; JZ <пропустить строку>  (ESI = кол-во уже
//   построенных фабрик этой категории в регионе)
// Ванильно: колониальный регион без построенных фабрик пропускается
// безусловно, минуя даже обычную проверку "Скрыть свободные" (эта
// часть уже нейтрализована — колониальность сама по себе перестала
// быть отдельным условием). Добавляем сюда третий, наш собственный
// флаг: если g_hideColonialStates включён, колониальный регион
// скрывается всегда, независимо от "Скрыть свободные" и наличия
// построенных фабрик — как и просил пользователь.
// ---------------------------------------------------------------

static const DWORD RVA_PRODLIST_HOOK = 0x2F424B;
static const DWORD RVA_PRODLIST_RESUME_SHOW = 0x2F4255;
static const DWORD RVA_PRODLIST_RESUME_SKIP = 0x2F42DE;
// Проверяем только первые 2 байта (JLE +8) — тот же якорь, что уже
// подтверждён рабочим в версии 2.8 простым байт-патчем. Байты 3-10
// (CMP ESI,EBX ; JZ) переписываются вслепую — их кодировка (39 DE
// или 3B F3 для CMP регистр-регистр — не проверялось напрямую) нашей
// логике не важна, старый код там больше не выполняется.
static const unsigned char PRODLIST_SIG[2] = { 0x7E, 0x08 };
static DWORD g_prodListResumeShow = 0;
static DWORD g_prodListResumeSkip = 0;

__declspec(naked) static void ProdListVisibilityThunk()
{
    __asm {
        cmp byte ptr [g_hideColonialStates], 0
        jz show
        cmp dword ptr [ecx + 0x84], 0
        jle show
        jmp dword ptr [g_prodListResumeSkip]
    show:
        jmp dword ptr [g_prodListResumeShow]
    }
}

static bool InstallProdListVisibilityHook()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_PRODLIST_HOOK);

    if (memcmp(hook, PRODLIST_SIG, sizeof(PRODLIST_SIG)) != 0)
    {
        Log("ProdListVisibilityHook: сигнатура не совпала - не патчим");
        return false;
    }

    g_prodListResumeShow = g_base + RVA_PRODLIST_RESUME_SHOW;
    g_prodListResumeSkip = g_base + RVA_PRODLIST_RESUME_SKIP;

    unsigned char patch[10];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&ProdListVisibilityThunk - ((DWORD)hook + 5);
    for (int i = 5; i < 10; ++i)
        patch[i] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("ProdListVisibilityHook: установлен");
    return true;
}


// ---------------------------------------------------------------
// Запрет конкретных фабрик в колониях по limit_by_local_supply.
//
// FUN_004d04b0(param_1=state, param_2=тип_производства, param_3)
// — единственная проверка, которая реально решает судьбу кнопки
// "Построить" (см. build_confirm_ignore_colonial). Внутри неё
// param_2+0x58 хранит порядковый номер типа производства из
// production_types.txt — подтверждено вживую диагностикой:
// живые значения 2/29/35 совпали с automobile_factory/
// fertilizer_factory/fishing_wharf при подсчёте БЕЗ учёта
// template-блоков (blocks, чьё имя встречается как значение
// "template = X" где-то в файле).
//
// Читаем production_types.txt сами (рядом с DLL, с проверкой
// mod\2\common\ как приоритетного оверрайда) и строим таблицу
// "разрешено ли строить в колонии" по тому же индексу: разрешено,
// если у типа явно указано limit_by_local_supply = yes.
// ---------------------------------------------------------------

static const int MAX_PRODUCTION_TYPES = 512;
static unsigned char g_limitByLocalSupply[MAX_PRODUCTION_TYPES];
static bool g_productionTypesLoaded = false;

// Игровой рантайм присваивает типам производства СВОЙ внутренний
// номер (поле +0x58 у объекта типа), который НЕ совпадает с порядком
// объявления в production_types.txt - подтверждено замером (cattle_factory
// в файле идёт под номером 51, а в рантайме её же объект несёт [+0x58]=1).
// Поэтому сверяемся не по индексу, а по имени: сохраняем имя каждого
// типа при разборе файла и на рантайме ищем совпадение по строке,
// которую движок хранит в самом объекте типа по смещению +0x20.
static const int PRODTYPE_NAME_MAX = 32;
static char g_productionTypeNames[MAX_PRODUCTION_TYPES][PRODTYPE_NAME_MAX];
static int  g_productionTypeCount = 0;

static const int OFF_PRODTYPE_NAME = 0x20;

static void GetOwnDllDirectory(char* outDir, size_t outSize)
{
    outDir[0] = 0;

    HMODULE hMod = 0;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&GetOwnDllDirectory, &hMod))
        return;

    char path[MAX_PATH];
    if (!GetModuleFileNameA(hMod, path, sizeof(path)))
        return;

    char* lastSlash = strrchr(path, '\\');
    if (!lastSlash)
        return;

    *lastSlash = 0;
    strncpy_s(outDir, outSize, path, _TRUNCATE);
}

static bool IsIdentChar(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

// Читает файл целиком в статический буфер. Возвращает false, если
// файла нет или он не помещается.
static bool ReadWholeFile(const char* path, char* buf, size_t bufSize, size_t* outLen)
{
    FILE* f = 0;
    if (fopen_s(&f, path, "rb") != 0 || !f)
        return false;

    *outLen = fread(buf, 1, bufSize - 1, f);
    buf[*outLen] = 0;
    fclose(f);
    return true;
}

static void ParseProductionTypes(const char* text, size_t len)
{
    // Проход 1: собрать имена блоков, использованных как "template = X"
    // (они сами по себе не являются типами производства и не участвуют
    // в нумерации).
    static char templateNames[256][64];
    int templateCount = 0;

    for (size_t i = 0; i + 8 < len; ++i)
    {
        if (strncmp(text + i, "template", 8) != 0)
            continue;
        if (i > 0 && IsIdentChar(text[i - 1]))
            continue;  // часть более длинного идентификатора
        if (IsIdentChar(text[i + 8]))
            continue;  // тоже часть более длинного идентификатора (с конца)

        size_t p = i + 8;
        while (p < len && (text[p] == ' ' || text[p] == '\t'))
            ++p;
        if (p >= len || text[p] != '=')
            continue;
        ++p;
        while (p < len && (text[p] == ' ' || text[p] == '\t'))
            ++p;

        size_t nameStart = p;
        while (p < len && IsIdentChar(text[p]))
            ++p;
        size_t nameLen = p - nameStart;

        if (nameLen > 0 && nameLen < 64 && templateCount < 256)
        {
            memcpy(templateNames[templateCount], text + nameStart, nameLen);
            templateNames[templateCount][nameLen] = 0;
            ++templateCount;
        }
    }

    // Проход 2: верхнеуровневые блоки "name = { ... }" по порядку;
    // пропускаем комментарии (# до конца строки) и шаблоны.
    int index = 0;
    int depth = 0;
    size_t i = 0;

    while (i < len)
    {
        char c = text[i];

        if (c == '#')
        {
            while (i < len && text[i] != '\n' && text[i] != '\r')
                ++i;
            continue;
        }

        if (depth == 0 && IsIdentChar(c) && (i == 0 || !IsIdentChar(text[i - 1])))
        {
            size_t nameStart = i;
            size_t p = i;
            while (p < len && IsIdentChar(text[p]))
                ++p;
            size_t nameLen = p - nameStart;

            size_t q = p;
            while (q < len && (text[q] == ' ' || text[q] == '\t' || text[q] == '\r' || text[q] == '\n'))
                ++q;

            if (q < len && text[q] == '=')
            {
                ++q;
                while (q < len && (text[q] == ' ' || text[q] == '\t' || text[q] == '\r' || text[q] == '\n'))
                    ++q;

                if (q < len && text[q] == '{')
                {
                    // Нашли верхнеуровневый блок. Найдём конец (парную '}'),
                    // попутно игнорируя комментарии, чтобы случайная '{'/'}'
                    // в тексте комментария не сбила подсчёт глубины.
                    size_t blockStart = q;
                    size_t j = q;
                    int localDepth = 0;

                    while (j < len)
                    {
                        char cj = text[j];
                        if (cj == '#')
                        {
                            while (j < len && text[j] != '\n' && text[j] != '\r')
                                ++j;
                            continue;
                        }
                        if (cj == '{')
                            ++localDepth;
                        else if (cj == '}')
                        {
                            --localDepth;
                            if (localDepth == 0)
                            {
                                ++j;
                                break;
                            }
                        }
                        ++j;
                    }

                    bool isTemplate = false;
                    for (int t = 0; t < templateCount; ++t)
                    {
                        size_t tlen = strlen(templateNames[t]);
                        if (tlen == nameLen && strncmp(templateNames[t], text + nameStart, nameLen) == 0)
                        {
                            isTemplate = true;
                            break;
                        }
                    }

                    if (!isTemplate)
                    {
                        bool hasLimitFlag = false;

                        // Ищем "limit_by_local_supply" ... "yes" внутри
                        // диапазона [blockStart, j) этого конкретного блока.
                        // Длина "limit_by_local_supply" - 21 символ (без
                        // учёта завершающего нуля strncmp здесь не нужен -
                        // раньше тут стояло 22, что сравнивало ЕЩЁ И нуль-
                        // терминатор литерала с реальным символом файла
                        // (обычно пробелом), из-за чего strncmp никогда не
                        // совпадал и флаг не находился ни разу).
                        for (size_t k = blockStart; k + 21 < j; ++k)
                        {
                            if (strncmp(text + k, "limit_by_local_supply", 21) != 0)
                                continue;
                            if (k > 0 && IsIdentChar(text[k - 1]))
                                continue;
                            if (IsIdentChar(text[k + 21]))
                                continue;

                            size_t r = k + 21;
                            while (r < j && (text[r] == ' ' || text[r] == '\t'))
                                ++r;
                            if (r < j && text[r] == '=')
                            {
                                ++r;
                                while (r < j && (text[r] == ' ' || text[r] == '\t'))
                                    ++r;
                                if (r + 3 <= j && strncmp(text + r, "yes", 3) == 0 &&
                                    !IsIdentChar(text[r + 3]))
                                    hasLimitFlag = true;
                            }
                            break;
                        }

                        if (index < MAX_PRODUCTION_TYPES)
                        {
                            g_limitByLocalSupply[index] = hasLimitFlag ? 1 : 0;

                            size_t copyLen = nameLen < (size_t)(PRODTYPE_NAME_MAX - 1)
                                ? nameLen : (size_t)(PRODTYPE_NAME_MAX - 1);
                            memcpy(g_productionTypeNames[index], text + nameStart, copyLen);
                            g_productionTypeNames[index][copyLen] = 0;

                            g_productionTypeCount = index + 1;
                        }

                        Log("  [%d] %.*s limit=%d", index, (int)nameLen, text + nameStart,
                            hasLimitFlag ? 1 : 0);

                        ++index;
                    }

                    i = j;
                    continue;
                }
            }

            i = p;
            continue;
        }

        if (c == '{')
            ++depth;
        else if (c == '}')
            --depth;

        ++i;
    }

    Log("ParseProductionTypes: разобрано %d типов производства (шаблонов пропущено: %d)",
        index, templateCount);
}

static void LoadProductionTypeLimits()
{
    if (g_productionTypesLoaded)
        return;
    g_productionTypesLoaded = true;

    for (int i = 0; i < MAX_PRODUCTION_TYPES; ++i)
        g_limitByLocalSupply[i] = 1;  // безопасный откат: неизвестный индекс - разрешаем

    char dir[MAX_PATH];
    GetOwnDllDirectory(dir, sizeof(dir));
    if (!dir[0])
    {
        Log("LoadProductionTypeLimits: не удалось определить каталог DLL");
        return;
    }

    static char fileBuf[1 << 20];
    size_t fileLen = 0;
    char path[MAX_PATH];

    sprintf_s(path, sizeof(path), "%s\\mod\\2\\common\\production_types.txt", dir);
    bool ok = ReadWholeFile(path, fileBuf, sizeof(fileBuf), &fileLen);

    if (!ok)
    {
        sprintf_s(path, sizeof(path), "%s\\common\\production_types.txt", dir);
        ok = ReadWholeFile(path, fileBuf, sizeof(fileBuf), &fileLen);
    }

    if (!ok)
    {
        Log("LoadProductionTypeLimits: production_types.txt не найден рядом с DLL");
        return;
    }

    Log("LoadProductionTypeLimits: читаю '%s' (%u байт)", path, (unsigned)fileLen);
    ParseProductionTypes(fileBuf, fileLen);
}

// Развилка внутри FUN_004d04b0 (абс. 0x4D04BC, RVA 0xD04BC):
//   CMP dword ptr[ECX+0x84],0 ; PUSH EBX ; PUSH ESI ; PUSH EDI
//   ; JLE +8 (0xD04C6, -> 0xD04D3 продолжение) ; иначе 0xD04C8: XOR AL,AL (return false)
//
// ВАЖНО: 0xD04C8 — это НЕ только цель нашей проверки. По всей
// остальной функции ЕЩЁ ДЕСЯТОК разных условий (доступность товара,
// разрешение правящей партии, лимит фабрик и т.д.) прыгают именно
// туда как на общий "return false". Первая версия патча по ошибке
// перезаписывала 8 байт НАЧИНАЯ С JLE — это стирало и сам 0xD04C8,
// ломая вообще ВСЕ эти несвязанные проверки (крах при любом вызове
// функции, что и объясняло вылет на загрузке партии). Правильный
// патч ставится РАНЬШЕ, с самого CMP (10 байт до JLE включительно:
// CMP+PUSH EBX+PUSH ESI+PUSH EDI), и НЕ трогает 0xD04C6+ вообще —
// поэтому 0xD04C8 остаётся целым, и путь "заблокировано" просто
// прыгает туда как обычно.
static const DWORD RVA_PRODTYPE_GATE_HOOK = 0xD04BC;
static const DWORD RVA_PRODTYPE_GATE_RESUME_ALLOW = 0xD04D3;
static const DWORD RVA_PRODTYPE_GATE_RESUME_BLOCK = 0xD04C8;
static const unsigned char PRODTYPE_GATE_SIG[10] =
    { 0x83, 0xB9, 0x84, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57 };
static DWORD g_prodTypeGateResumeAllow = 0;
static DWORD g_prodTypeGateResumeBlock = 0;

// Указатель param_2 ([EBP+0xC] внутри FUN_004d04b0) - это и есть
// объект типа производства; имя типа (как в production_types.txt)
// хранится в нём самом по смещению OFF_PRODTYPE_NAME в виде обычной
// C-строки. Сверяем эту строку с именами, сохранёнными при разборе
// файла, и смотрим найденный по имени индекс в g_limitByLocalSupply -
// НЕ читаем числовой индекс из самого объекта (см. комментарий у
// объявления g_productionTypeNames: он не совпадает с файловым).
// Точечные исключения сверх limit_by_local_supply: типы, которым
// тоже нужно разрешить постройку в колонии, но заводить для них
// целый отдельный флаг в production_types.txt не стали - patch
// только по имени. По умолчанию это fishery (is_coastal = yes);
// список редактируется из ini (PROD_TYPE_GATE_EXTRA_WHITELIST) без
// пересборки DLL - см. ParseExtraWhitelist. Используется только когда
// PROD_TYPE_GATE_ALLOW_ALL=0 (иначе разрешены все типы, до этого
// списка дело не доходит).
static const int MAX_EXTRA_WHITELIST = 16;
static const int EXTRA_WHITELIST_NAME_MAX = 64;
static char g_extraWhitelistNames[MAX_EXTRA_WHITELIST][EXTRA_WHITELIST_NAME_MAX] = { "fishery" };
static int g_extraWhitelistCount = 1;

static int __cdecl IsProdTypeWhitelistedByName(void* typePtr)
{
    if (!typePtr)
        return 0;

    const char* src = (const char*)typePtr + OFF_PRODTYPE_NAME;
    char name[PRODTYPE_NAME_MAX];

    int i = 0;
    for (; i < PRODTYPE_NAME_MAX - 1; ++i)
    {
        char c = src[i];
        if (c == 0)
            break;
        name[i] = c;
    }
    name[i] = 0;

    for (int e = 0; e < g_extraWhitelistCount; ++e)
        if (_stricmp(g_extraWhitelistNames[e], name) == 0)
            return 1;

    for (int t = 0; t < g_productionTypeCount; ++t)
    {
        if (strcmp(g_productionTypeNames[t], name) == 0)
            return g_limitByLocalSupply[t] ? 1 : 0;
    }
    return 0;
}

static DWORD g_prodTypeGateWhitelisted = 0;

// Плоская копия g_settings.prodTypeGateAllowAll - в наked-asm проще
// и безопаснее читать отдельный global bool, чем поле структуры.
static unsigned char g_prodTypeGateAllowAll = 1;

__declspec(naked) static void ProdTypeGateThunk()
{
    __asm {
        // Сначала, ДО воспроизведения затёртых байт и ветвления,
        // одним изолированным блоком считаем "разрешено ли по имени".
        // ECX (состояние) сохраняем на всё время блока одной парой
        // push/pop - так же, как это делала более ранняя диагностика,
        // которая отработала без сбоев; вложенные push/pop вокруг
        // вызова C-функции ПОСРЕДИ уже разветвлённой логики (предыдущая
        // версия патча) на практике приводили к падению игры при
        // загрузке партии - как именно, не установлено, но структура
        // "один вызов - один save/restore - только потом ветвление"
        // проверена и безопасна.
        push ecx

        // PROD_TYPE_GATE_ALLOW_ALL=1 - разрешаем любой тип, дальше по
        // имени вообще не проверяем (белый список ниже используется
        // только в противоположном режиме, ALLOW_ALL=0).
        cmp byte ptr [g_prodTypeGateAllowAll], 0
        jz check_by_name
        mov eax, 1
        jmp store_result

    check_by_name:
        mov eax, dword ptr [ebp + 0x0c]
        test eax, eax
        jz faulty_allow
        push 0x5C
        push eax
        mov edx, g_fnIsBadReadPtr
        test edx, edx
        jz faulty_allow_clean8
        call edx
        test eax, eax
        jnz faulty_allow
        push dword ptr [ebp + 0x0c]
        call IsProdTypeWhitelistedByName
        add esp, 4
        jmp store_result
    faulty_allow_clean8:
        add esp, 8
    faulty_allow:
        mov eax, 1   // указатель плохой/пуст - безопасный откат: разрешаем, как раньше
    store_result:
        mov dword ptr [g_prodTypeGateWhitelisted], eax

        pop ecx

        // Воспроизводим переписанные байты (это НЕ цель прыжков извне,
        // так что их можно спокойно исполнить здесь же).
        cmp dword ptr [ecx + 0x84], 0
        push ebx
        push esi
        push edi
        jle allow
        cmp dword ptr [g_prodTypeGateWhitelisted], 0
        jnz allow
        jmp dword ptr [g_prodTypeGateResumeBlock]
    allow:
        jmp dword ptr [g_prodTypeGateResumeAllow]
    }
}

static bool InstallProdTypeGateHook()
{
    LoadProductionTypeLimits();

    g_prodTypeGateAllowAll = g_settings.prodTypeGateAllowAll ? 1 : 0;

    g_fnIsBadReadPtr = SafeIsBadReadPtr;

    unsigned char* hook = (unsigned char*)(g_base + RVA_PRODTYPE_GATE_HOOK);

    if (memcmp(hook, PRODTYPE_GATE_SIG, sizeof(PRODTYPE_GATE_SIG)) != 0)
    {
        Log("ProdTypeGateHook: сигнатура не совпала - не патчим");
        return false;
    }

    g_prodTypeGateResumeAllow = g_base + RVA_PRODTYPE_GATE_RESUME_ALLOW;
    g_prodTypeGateResumeBlock = g_base + RVA_PRODTYPE_GATE_RESUME_BLOCK;

    unsigned char patch[10];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&ProdTypeGateThunk - ((DWORD)hook + 5);
    for (int i = 5; i < 10; ++i)
        patch[i] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("ProdTypeGateHook: установлен");
    return true;
}


// ---------------------------------------------------------------
// Подменённые слоты видов
// ---------------------------------------------------------------

// Слот Update: __thiscall без стековых аргументов, плоский ret.
typedef void(__fastcall* tUpdate)(void* ecx, void* edx);

// Слот подсказки: __thiscall с двумя стековыми аргументами и ret 8 —
// буфер под результат и элемент под курсором.
typedef void* (__fastcall* tTooltip)(void* ecx, void* edx, void* retBuf, void* element);

static void* g_origSlot[MAX_VIEWS] = { 0 };
static void* g_configuredView[MAX_VIEWS] = { 0 };
static int   g_updateSeen[MAX_VIEWS] = { 0 };

static void OnViewUpdate(int viewIndex, void* view)
{
    if (!view)
        return;

    if (g_updateSeen[viewIndex] < 2)
    {
        ++g_updateSeen[viewIndex];
        Log("Update[%s]: вызван, view=%08X",
            VIEWS[viewIndex].name, (DWORD)(DWORD_PTR)view);
    }

    // Указатель сменился — вид пересоздан, например новой партией.
    if (view == g_configuredView[viewIndex])
        return;

    if (g_settings.buttons && SetupButtons(viewIndex, view))
        g_configuredView[viewIndex] = view;

    if (viewIndex == 2 && view != g_hideColonialConfiguredView)
    {
        if (SetupHideColonialButton(view))
            g_hideColonialConfiguredView = view;
    }
}

#define VIEW_THUNKS(n)                                              \
    static void __fastcall Update##n(void* view, void* edx)         \
    {                                                               \
        if (n < VIEW_COUNT)                                         \
            OnViewUpdate(n, view);                                  \
        if (g_origSlot[n])                                          \
            ((tUpdate)g_origSlot[n])(view, 0);                      \
    }                                                               \
    static void* __fastcall Tip##n(void* view, void* edx,           \
                                   void* retBuf, void* element)     \
    {                                                               \
        if (n < VIEW_COUNT)                                         \
            OnViewUpdate(n, view);                                  \
        void* result = g_origSlot[n]                                \
            ? ((tTooltip)g_origSlot[n])(view, 0, retBuf, element)    \
            : retBuf;                                               \
        OnTooltip(n, result, element);                               \
        return result;                                              \
    }

VIEW_THUNKS(0) VIEW_THUNKS(1) VIEW_THUNKS(2) VIEW_THUNKS(3)

static void* const UPDATE_THUNKS[MAX_VIEWS] =
{
    (void*)&Update0, (void*)&Update1, (void*)&Update2, (void*)&Update3,
};

static void* const TOOLTIP_THUNKS[MAX_VIEWS] =
{
    (void*)&Tip0, (void*)&Tip1, (void*)&Tip2, (void*)&Tip3,
};


// ---------------------------------------------------------------
// Фильтр списка решений
//
// Слот 0x18 в vtable CDecision — заглушка, всегда возвращавшая 1.
// Движок зовёт её и при отрисовке списка, и при исполнении, поэтому
// различаем по адресу возврата: он детерминирован и одинаков на всех
// машинах, в отличие от таймера, который ломал мультиплеер.
// ---------------------------------------------------------------

typedef char(__fastcall* tIsValid)(void* ecx, void* edx);
static tIsValid g_origIsValid = 0;

static const int OFF_DECISION_NAME = 0x08;

static const char* DecisionName(void* decision)
{
    const char* p = (const char*)decision + OFF_DECISION_NAME;

    unsigned res = *(const unsigned*)(p + 0x14);
    unsigned size = *(const unsigned*)(p + 0x10);

    // Грубая проверка на вменяемость: имена решений короткие, а
    // ёмкость не бывает меньше длины.
    if (size > 250 || res < size)
        return "";

    if (res > 15)
    {
        p = *(const char* const*)p;
        if (!p)
            return "";
    }

    return p;
}

static char __fastcall MyDecisionIsValid(void* decision, void* edx)
{
    if (!decision)
        return 1;

    const char* name = DecisionName(decision);
    if (!*name)
        return 1;

    bool mine = false;
    for (int i = 0; i < BUTTON_COUNT; ++i)
    {
        if (strcmp(name, BUTTONS[i].decision) == 0)
        {
            mine = true;
            break;
        }
    }

    if (!mine)
        return 1;

    DWORD caller = (DWORD)(DWORD_PTR)_ReturnAddress();
    bool fromList = caller >= g_base + RVA_POLITICS_DRAW_BEGIN
        && caller < g_base + RVA_POLITICS_DRAW_END;

    return fromList ? 0 : 1;
}




// ---------------------------------------------------------------
// Процентный шаг изменения цены
//
// Ванильно шаг фиксирован: по 0x0125B9E0 лежит int64 = 328, то есть
// 0.01 в фиксированной точке (0.01 * 2^15). Перехватываем место, где
// в ECX:EAX уже лежит текущая цена, и переписываем константу на долю
// от неё. Константу не читает никто, кроме этой функции.
//
// Арифметика целочисленная, от времени и порядка событий не зависит:
// при одинаковой DLL все клиенты получают побитно одинаковый результат.
// ---------------------------------------------------------------

// Шаг в сотых долях процента: 25 = 0.25% в день, 100 = 1%.
static const int PRICE_BASIS_POINTS = 25;

static const DWORD PRICE_MUL = (DWORD)((PRICE_BASIS_POINTS * 65536LL) / 10000);

static const DWORD RVA_PRICE_HOOK = 0x82BA9;   // SAR EDX,0Fh
static const DWORD RVA_PRICE_RESUME = 0x82BAE;   // SUB EDI,[delta]
static const DWORD RVA_PRICE_DELTA = 0xE5B9E0;  // int64, младшее слово

static const unsigned char PRICE_SIG[5] = { 0xC1, 0xFA, 0x0F, 0x8B, 0xF8 };

static bool InstallPriceDelta()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_PRICE_HOOK);

    if (memcmp(hook, PRICE_SIG, sizeof(PRICE_SIG)) != 0)
    {
        Log("PriceDelta: сигнатура не совпала - не патчим");
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
        return false;

    DWORD deltaLo = g_base + RVA_PRICE_DELTA;
    DWORD deltaHi = deltaLo + 4;

    int n = 0;

    // Восстанавливаем то, что перекрыли прыжком.
    cave[n++] = 0xC1; cave[n++] = 0xFA; cave[n++] = 0x0F;   // sar edx, 0Fh
    cave[n++] = 0x8B; cave[n++] = 0xF8;                     // mov edi, eax

    // EAX и ECX — цена, EDX — целевая цена; MUL затирает EDX.
    cave[n++] = 0x50;                                       // push eax
    cave[n++] = 0x51;                                       // push ecx
    cave[n++] = 0x52;                                       // push edx

    cave[n++] = 0xB9;                                       // mov ecx, PRICE_MUL
    *(DWORD*)(cave + n) = PRICE_MUL; n += 4;
    cave[n++] = 0xF7; cave[n++] = 0xE1;                     // mul ecx
    cave[n++] = 0x0F; cave[n++] = 0xAC; cave[n++] = 0xD0;
    cave[n++] = 0x10;                                       // shrd eax, edx, 16
    cave[n++] = 0xC1; cave[n++] = 0xEA; cave[n++] = 0x10;   // shr edx, 16

    // У дешёвых товаров доля округляется в ноль, и цена застыла бы.
    cave[n++] = 0x85; cave[n++] = 0xD2;                     // test edx, edx
    cave[n++] = 0x75; cave[n++] = 0x05;                     // jnz store
    cave[n++] = 0x85; cave[n++] = 0xC0;                     // test eax, eax
    cave[n++] = 0x75; cave[n++] = 0x01;                     // jnz store
    cave[n++] = 0x40;                                       // inc eax

    cave[n++] = 0xA3;                                       // mov [deltaLo], eax
    *(DWORD*)(cave + n) = deltaLo; n += 4;
    cave[n++] = 0x89; cave[n++] = 0x15;                     // mov [deltaHi], edx
    *(DWORD*)(cave + n) = deltaHi; n += 4;

    cave[n++] = 0x5A;                                       // pop edx
    cave[n++] = 0x59;                                       // pop ecx
    cave[n++] = 0x58;                                       // pop eax

    cave[n++] = 0xE9;                                       // jmp обратно
    *(DWORD*)(cave + n) = (g_base + RVA_PRICE_RESUME) - (DWORD)(cave + n + 4);
    n += 4;

    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)cave - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("PriceDelta: %d сотых процента, множитель %u",
        PRICE_BASIS_POINTS, PRICE_MUL);
    return true;
}


// ---------------------------------------------------------------
// Экспоненциальный шаг изменения цены (альтернатива InstallPriceDelta)
//
// Та же самая точка перехвата (RVA_PRICE_HOOK/RVA_PRICE_RESUME,
// PRICE_SIG), поэтому взаимоисключающе с ENABLE_PRICE_DELTA - см.
// проверку в Install(). Получен готовым дизасмом (radare2), байты
// сверены вручную побайтово с исходным дампом:
//
//   sar edx, 0xf              ; воспроизводим перекрытое
//   mov edi, eax              ; воспроизводим перекрытое
//   shrd edi, ecx, 8          ; НОВОЕ: EDI = (ECX:EAX) >> 8, младшие 32 бита
//   mov [deltaLo], edi        ; записываем в младшее слово шага
//   mov edi, ecx
//   sar edi, 7                ; НОВОЕ: старшее слово = ECX >> 7 (не 8!)
//   mov [deltaHi], edi        ; записываем в старшее слово шага
//   mov edi, eax              ; воспроизводим перекрытое ещё раз
//   sub edi, [deltaLo]        ; воспроизводим перекрытое (RVA_PRICE_RESUME)
//   jmp RVA_PRICE_RESUME
//
// ECX:EAX на входе — текущая цена (см. комментарий у InstallPriceDelta).
// Шаг (0x0125B9E0/E4, тот же int64, что и в InstallPriceDelta) отсюда
// больше не константа, а производная от самой цены через сдвиги —
// то есть шаг растёт вместе с ценой, а не остаётся фиксированным
// абсолютным числом. Несимметричные сдвиги (8 для младшего слова,
// 7 для старшего) взяты как есть из готового патча - самостоятельно
// вывести точный процент из этого несоответствия не пытались, байты
// просто перенесены без изменений на другой (нашей) адрес пещеры.
// ---------------------------------------------------------------

static bool InstallExponentialPriceDelta()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_PRICE_HOOK);

    if (memcmp(hook, PRICE_SIG, sizeof(PRICE_SIG)) != 0)
    {
        Log("ExponentialPriceDelta: сигнатура не совпала - не патчим");
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
        return false;

    DWORD deltaLo = g_base + RVA_PRICE_DELTA;
    DWORD deltaHi = deltaLo + 4;

    int n = 0;

    cave[n++] = 0xC1; cave[n++] = 0xFA; cave[n++] = 0x0F;   // sar edx, 0Fh
    cave[n++] = 0x89; cave[n++] = 0xC7;                     // mov edi, eax

    cave[n++] = 0x0F; cave[n++] = 0xAC; cave[n++] = 0xCF; cave[n++] = 0x08; // shrd edi, ecx, 8

    cave[n++] = 0x89; cave[n++] = 0x3D;                     // mov [deltaLo], edi
    *(DWORD*)(cave + n) = deltaLo; n += 4;

    cave[n++] = 0x89; cave[n++] = 0xCF;                     // mov edi, ecx
    cave[n++] = 0xC1; cave[n++] = 0xFF; cave[n++] = 0x07;   // sar edi, 7

    cave[n++] = 0x89; cave[n++] = 0x3D;                     // mov [deltaHi], edi
    *(DWORD*)(cave + n) = deltaHi; n += 4;

    cave[n++] = 0x89; cave[n++] = 0xC7;                     // mov edi, eax

    cave[n++] = 0x2B; cave[n++] = 0x3D;                     // sub edi, [deltaLo]
    *(DWORD*)(cave + n) = deltaLo; n += 4;

    cave[n++] = 0xE9;                                       // jmp обратно
    *(DWORD*)(cave + n) = (g_base + RVA_PRICE_RESUME) - (DWORD)(cave + n + 4);
    n += 4;

    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)cave - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("ExponentialPriceDelta: установлен, пещера %08X", (DWORD)(DWORD_PTR)cave);
    return true;
}


// ---------------------------------------------------------------
// Байтовые правки exe
//
// Смещения заданы как в exe-модах ZombieFreak115 — как смещения в
// ФАЙЛЕ. Переводим их через таблицу секций PE в рантайме.
// Правки меняют симуляцию, поэтому DLL должна быть одинаковой у всех.
// ---------------------------------------------------------------

// Правка задаётся либо смещением в файле (как в exe-модах
// ZombieFreak115), либо сразу RVA. Заполняется одно из двух.
struct BytePatch
{
    const char* name;
    DWORD         fileOffset;
    DWORD         rva;
    int           len;
    unsigned char expect[16];
    unsigned char replace[16];
    bool          enabled;
};

static BytePatch EXE_PATCHES[] =
{
    // Постоянно включённый debug alwaysaddwargoal.
    { "always_add_wargoals", 0x137EFF, 0, 1, { 0x00 }, { 0x02 }, true },

    // Было: mov [esp+20h], ecx — пересылка ослабленного снабжения
    // на следующую бригаду в стеке.
    { "land_reinforce",      0x1C809B, 0, 4, { 0x89, 0x4C, 0x24, 0x20 },
                                             { 0x90, 0x90, 0x90, 0x90 }, true },

                                             // 89 -> 8B: направление пересылки меняется на обратное.
                                             { "naval_reinforce",     0x1C7F1C, 0, 1, { 0x89 }, { 0x8B }, true },

    // Относительный максимум цены (double, значение хранится x16384):
    // было ~10x базовой цены, пробовали x100 (сломало экономику - см.
    // историю правок и common/defines.lua) и x20 (в игре видимый потолок
    // в Trade-окне всё равно остался x10 - разбирались через Ghidra,
    // константа реально читается в двух местах в FUN_00482930/0082f430,
    // но точную причину "почему всё равно x10" статическим анализом
    // выяснить не удалось - функция слишком плотная). Ставим x40 как
    // эмпирическую проверку: если видимый потолок сдвинется - константа
    // всё же влияет, просто нелинейно/с обходным путём; если останется
    // x10 - дело не в этой константе вообще. rva задан напрямую (адрес
    // в Ghidra 0xE45C28 минус imagebase 0x400000).
    { "max_relative_price",  0, 0xA45C28, 8,
        { 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x41 },
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x41 }, true },

    // Ежемесячный рост plurality от средней сознательности страны.
    // Было: add eax,[ebx+0x1A8]  (eax = вклад от consciousness, [ebx+0x1A8] = plurality)
    // Стало: mov eax,[ebx+0x1A8] — вклад отбрасывается, plurality не меняется
    // от этой формулы; остальная часть функции (клампы, прочие поля) не тронута,
    // второй писатель plurality (скриптовый эффект "plurality = X" из событий,
    // rva 0x496470) тоже не тронут.
    { "consciousness_plurality_growth", 0, 0x10C5DE, 1, { 0x03 }, { 0x8B }, true },

    // Локальный показатель пополнения (FUN_005D7420, ветка LAB_005d74bb):
    // было 1000 (100.0%, "allied or occupied territory" из вики), ставим
    // 1500 (150.0%). Та же ветка используется и для юнитов тега REB в одном
    // узком случае (совпадение провинции с "домашней" для родительской
    // страны) — разделить без добавления ветвления нельзя, это одна и та
    // же инструкция mov eax,imm32.
    { "allied_reinforce_150", 0, 0x1D74BE, 5,
        { 0xB8, 0xE8, 0x03, 0x00, 0x00 },
        { 0xB8, 0xDC, 0x05, 0x00, 0x00 }, true },

    // Разрешить строить фабрики в колониальных регионах.
    // Обе функции чек-листа постройки (FUN_004d06a0 и FUN_0052e9f0)
    // независимо инлайнят одну и ту же проверку "state+0x84 > 0" для
    // пункта "Неколониальная область" (BUILD_COLONIAL). Проверено вживую
    // (диагностический хук): для колониального региона +0x84 = 2, и
    // результат SETZ = 0 передаётся в отрисовщик иконки как "крестик" —
    // то есть 0 = крестик, 1 = галочка (обратно тому, что предполагалось
    // изначально). Патчим SETZ так, чтобы результат был всегда 1.
    // FUN_004d06a0 подтверждённо не вызывается для кнопки "+" (0
    // попаданий), но правим и её для согласованности — вдруг
    // используется в другом месте (чек-лист "Расширить"?).
    { "build_factory_ignore_colonial_1", 0, 0xD0C57, 3,
        { 0x0F, 0x94, 0xC1 },
        { 0xB1, 0x01, 0x90 }, true },
    { "build_factory_ignore_colonial_2", 0, 0x12FA4E, 3,
        { 0x0F, 0x94, 0xC0 },
        { 0xB0, 0x01, 0x90 }, true },

    // Третья, независимая копия той же проверки — но эта единственная
    // реально решает, активна ли кнопка "+" (FUN_0052e960, вызывается
    // из FUN_0073f300 для виджета "build_factory_button" и напрямую
    // определяет, какой из виртуальных методов включения/выключения
    // кнопки будет вызван). Патчи _1/_2 выше правят только текст
    // чек-листа, на саму кнопку не влияют.
    // Было: CMP dword ptr [EAX+0x84],0 ; JG +0x5F (если колония — сразу
    // возврат "выключено", минуя все остальные условия). NOP'аем JG,
    // чтобы колониальность не мешала остальной цепочке проверок
    // (цивилизованность / лимит фабрик / разрешение правящей партии).
    { "build_factory_button_enable_ignore_colonial", 0, 0x12E977, 2,
        { 0x7F, 0x5F },
        { 0x90, 0x90 }, true },

    // FUN_004d0e70 (абс. 0x4D0E70) - отдельная, независимая от
    // FUN_004d04b0 проверка, вызываемая ТОЛЬКО для production_type с
    // заполненным полем +300 (привязка к "локальному источнику" -
    // судя по всему заполняется именно у limit_by_local_supply=yes
    // типов, поэтому обычные фабрики её вообще не проходят). Внутри:
    // if (тип_источника == 2 && состояние.colonial > 0 && <совпадение
    // владельца/тега>) return false. Это и есть настоящая причина
    // "не могу построить collapsed-РГО-фабрику именно в колонии" -
    // никак не связанная ни с нашим гейтом, ни с limit_by_local_supply
    // как таковым. Меняем JLE (0x7E) на безусловный JMP (0xEB) на
    // rva 0xD0E9D - переход на 0xD0ED4 (пропуск блокировки) теперь
    // происходит всегда, независимо от colonial, а остальные условия
    // функции (владелец/тег и т.д.) продолжают работать как раньше.
    { "local_supply_factory_ignore_colonial", 0, 0xD0E9D, 2,
        { 0x7E, 0x35 },
        { 0xEB, 0x35 }, true },

    // Разрешить нецивилизованным странам строить фабрики.
    //
    // FUN_0052e960 (rva 0x12E960) - единственная функция, реально
    // решающая, активна ли кнопка "+" (см. коммент выше про
    // local_supply_factory_ignore_colonial - она из той же серии
    // проверок). Первым делом: cmp byte ptr[EDI+0x12D0],0 ; jz -> сразу
    // "выключено", если страна (EDI, "мы") не цивилизована. NOP'аем JZ,
    // чтобы гейт цивилизованности не мешал остальной цепочке проверок
    // (колониальность / лимит фабрик / прочее) - как и в
    // build_factory_button_enable_ignore_colonial для колоний.
    { "build_factory_ignore_uncivilized_button", 0, 0x12E96E, 2,
        { 0x74, 0x68 },
        { 0x90, 0x90 }, true },

    // FUN_0052e9f0 (rva 0x12E9F0) - функция чек-листа постройки,
    // формирует пункт "Цивилизованная страна". Ветка "наша территория"
    // (rva 0x12EA67): mov al,[ESI+0x12D0] читает наш флаг цивилизации
    // напрямую в результат пункта чек-листа. Подменяем на mov al,1 -
    // пункт всегда показывает галочку, независимо от статуса.
    { "build_factory_checklist_uncivilized_own", 0, 0x12EA67, 6,
        { 0x8A, 0x86, 0xD0, 0x12, 0x00, 0x00 },
        { 0xB0, 0x01, 0x90, 0x90, 0x90, 0x90 }, true },

    // Та же функция, ветка "не наша территория" (rva 0x12F0D4):
    // cmp byte ptr[ESI+0x12D0],0 ; jz -> пункт=0, если МЫ не
    // цивилизованы (независимо от статуса владельца провинции).
    // NOP'аем JZ - оставляем в силе только проверку цивилизации
    // владельца провинции, которая к этому патчу не относится.
    { "build_factory_checklist_uncivilized_other", 0, 0x12F0D4, 2,
        { 0x74, 0x0C },
        { 0x90, 0x90 }, true },


    // Настоящий гейт "Построить" найден: FUN_0052c9b0 (реальная
    // проверка "можно ли построить фабрику этого типа сейчас", в
    // отличие от FUN_0052e960, который только решает состояние
    // кнопки "+") вызывает ПОСЛЕДОВАТЕЛЬНО FUN_0052e960() (уже
    // пропатчена тремя патчами выше), FUN_0052ca30() и проверку
    // денег. FUN_0052ca30 - тонкая обёртка над FUN_004d04b0 (общий
    // чек-лист "тип доступен в этом штате", патчи _ignore_colonial
    // выше правят другую его часть), которая ДОБАВЛЯЕТ СВОЮ
    // НЕЗАВИСИМУЮ проверку civilized ПЕРЕД вызовом FUN_004d04b0:
    // cmp byte ptr[EDI+0x12D0],0 ; jnz continue ; xor al,al ; ret
    // (если не цивилизованы - сразу false, FUN_004d04b0 не
    // вызывается вовсе). Меняем JNZ (0x75) на безусловный JMP
    // (0xEB) - тот же байт смещения, тот же путь, что и при
    // civilized != 0.
    { "build_factory_ignore_uncivilized_can_build", 0, 0x12CA3E, 2,
        { 0x75, 0x08 },
        { 0xEB, 0x08 }, true },

    // Разрешить нецивилизованным странам исследовать технологии.
    //
    // human_player_patch, byte-в-byte как был дан изначально (адрес,
    // длина, expect/replace - без изменений). ПО ЗАПРОСУ пользователя,
    // несмотря на то, что этот конкретный адрес (0x3A9B57, как fileOffset)
    // не резолвится в Ghidra как код в этой сборке - есть отдельная,
    // самостоятельно найденная и рабочая альтернатива (xref на строку
    // "UNCIV_CANT_RESEARCH" -> FUN_007a9950, rva 0x3A9F21, near jmp,
    // см. историю правок этого файла), но её результат в игре не
    // подтверждён (возможен второй, независимый гейт внутри
    // FUN_00569920). Сигнатура ниже, вероятнее всего, не совпадёт при
    // запуске - InstallExePatches просто пропустит эту запись и
    // залогирует "сигнатура не совпала", без вреда.
    { "allow_unciv_tech_research", 0x3A9B57, 0, 1,
        { 0x75 },
        { 0xEB }, false },

    // Доля дохода РГО, уходящая владельцам/аристократам - меняет
    // масштаб формулы в FUN_004ee990 (VA 0x4EE990). Проверено через
    // Ghidra (decompile_function_by_address на VA 0x4EEA2B):
    //
    //   lVar10 = __alldiv(uVar1<<0x1f, uVar1>>1,           ; числитель:  uVar1 * 2^31 (64-бит)
    //                      uVar5<<0xf,  sign(uVar5)<<0xf | uVar5>>0x11); ; знаменатель: (int64)uVar5 * 2^15
    //   ; результат = (uVar1/uVar5) * 2^16, дальше клампится сверху
    //   ; константой (DAT_0125d758/5c) - потолок этим патчем не трогается
    //
    // uVar1 - количество нужного типа попов (владельцы/аристократы),
    // uVar5 - размер занятой рабочей силы (сравнение через отношение,
    // не фиксированный процент из defines).
    //
    // Патч _1/_2 меняет сдвиг числителя 31->17 бит (owners<<0x1F
    // -> owners<<0x11 в двух половинах 64-битного сдвига). Патчи
    // _3/_4/_5 убирают сдвиг+маску знаменателя (15 бит) целиком -
    // uVar5 входит в деление НЕмасштабированным.
    //
    // Итог: result = (uVar1/uVar5) * 2^17 - РОВНО в 2 раза (+100%)
    // больше ванильного (uVar1/uVar5) * 2^16, при том же отношении
    // владельцы/рабочие. Потолок (кламп) не меняется - там, где
    // ванильное значение уже упиралось в потолок, эффекта не будет.
    { "aristocrat_income_share_patch_1", 0, 0xEEA2B, 1, { 0x1F }, { 0x11 }, false },
    { "aristocrat_income_share_patch_2", 0, 0xEEA2E, 1, { 0x1F }, { 0x11 }, false },
    { "aristocrat_income_share_patch_3", 0, 0xEEA32, 4,
        { 0x0F, 0xA4, 0xC2, 0x0F },
        { 0x90, 0x90, 0x90, 0x90 }, false },
    { "aristocrat_income_share_patch_4", 0, 0xEEA37, 3,
        { 0xC1, 0xE0, 0x0F },
        { 0x90, 0x90, 0x90 }, false },
    { "aristocrat_income_share_patch_5", 0, 0xEEA3C, 6,
        { 0x81, 0xE7, 0x00, 0x80, 0xFF, 0xFF },
        { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }, false },

    // Чек-сумма (в углу экрана) уходит на +1 после каждого входа в
    // партию/лобби, из-за чего она не совпадает с той, что была при
    // запуске - и в мультиплеере приходится перезапускать клиент,
    // чтобы чек-суммы у игроков снова совпали. Воспроизводится и на
    // ванили. Найдено через живое логирование из нашего же DLL +
    // подтверждено Cheat Engine ("find out what writes to this
    // address" на адрес аккумулятора чек-суммы):
    //
    // Конструктор CBackEndIdler (FUN_005f8110, грузит
    // interface/backend.gui - экран загрузки между меню и партией,
    // т.е. срабатывает на каждый вход) в конце безусловно делает
    // (*(int*)(param_4+0x30))++ - param_4 это тот же объект, чьё
    // поле +0x30 отдельно читает FUN_006377a0 как итоговое целое
    // чек-суммы перед тем, как построить текст "Checksum is X".
    // Кроме этих двух мест (чтение в FUN_006377a0, инкремент здесь)
    // поле нигде больше не встречается - похоже на посторонний
    // счётчик загрузок, случайно заведённый в то же поле.
    //
    // Убираем ровно "INC EAX" (1 байт), оставляя чтение и запись
    // того же значения обратно - безобидная no-op пара вместо
    // инкремента, минимальное вмешательство.
    { "checksum_drift_fix", 0, 0x1F8268, 1,
        { 0x40 },
        { 0x90 }, true },
};

static const int EXE_PATCH_COUNT = sizeof(EXE_PATCHES) / sizeof(EXE_PATCHES[0]);


// ---------------------------------------------------------------
// Загрузка/сохранение настроек (v2dll_settings.ini)
//
// Ключи ENABLE_* и семь именованных PATCH_* правят поля g_settings.
// Любой другой ключ PATCH_<ИМЯ> ищется (без учёта регистра) в
// таблице EXE_PATCHES выше и правит BytePatch::enabled найденной
// записи - поэтому весь блок объявлен здесь, после таблицы, а не
// в начале файла.
// ---------------------------------------------------------------

static bool ParseBoolValue(const char* value)
{
    while (*value == ' ' || *value == '\t')
        ++value;
    return atoi(value) != 0;
}

// Формат значения: имена типов производства через запятую, например
// "fishery,some_other_type". Пробелы вокруг имён и запятых игнорируются.
static void ParseExtraWhitelist(const char* value)
{
    g_extraWhitelistCount = 0;
    const char* p = value;

    while (*p && g_extraWhitelistCount < MAX_EXTRA_WHITELIST)
    {
        while (*p == ' ' || *p == '\t' || *p == ',')
            ++p;
        if (!*p || *p == '\n' || *p == '\r')
            break;

        int i = 0;
        while (*p && *p != ',' && *p != '\n' && *p != '\r' && i < EXTRA_WHITELIST_NAME_MAX - 1)
            g_extraWhitelistNames[g_extraWhitelistCount][i++] = *p++;

        while (i > 0 && (g_extraWhitelistNames[g_extraWhitelistCount][i - 1] == ' ' ||
                         g_extraWhitelistNames[g_extraWhitelistCount][i - 1] == '\t'))
            --i;
        g_extraWhitelistNames[g_extraWhitelistCount][i] = '\0';

        if (i > 0)
            ++g_extraWhitelistCount;

        while (*p && *p != ',')
            ++p;
    }
}

static void ApplySetting(const char* key, const char* value)
{
    bool v = ParseBoolValue(value);

    if (_stricmp(key, "ENABLE_LOG") == 0)                  { g_settings.log            = v; return; }
    if (_stricmp(key, "ENABLE_BUTTONS") == 0)               { g_settings.buttons        = v; return; }
    if (_stricmp(key, "ENABLE_DECISION_FILTER") == 0)       { g_settings.decisionFilter = v; return; }
    if (_stricmp(key, "ENABLE_PRICE_DELTA") == 0)           { g_settings.priceDelta     = v; return; }
    if (_stricmp(key, "ENABLE_POP_DISPLAY") == 0)           { g_settings.popDisplay     = v; return; }
    if (_stricmp(key, "ENABLE_VERSION_LABEL") == 0)         { g_settings.versionLabel   = v; return; }

    if (_stricmp(key, "PATCH_OCCUPIED_REINFORCE_SPLIT") == 0) { g_settings.patchOccupiedReinforceSplit = v; return; }
    if (_stricmp(key, "PATCH_ALLY_OWNER_CHECK") == 0)          { g_settings.patchAllyOwnerCheck         = v; return; }
    if (_stricmp(key, "PATCH_CIVILIZE_NULL_CHECK") == 0)       { g_settings.patchCivilizeNullCheck      = v; return; }
    if (_stricmp(key, "PATCH_NULL_VTABLE_UI") == 0)            { g_settings.patchNullVtableUi           = v; return; }
    if (_stricmp(key, "PATCH_GRAPH_POINT_CLAMP") == 0)         { g_settings.patchGraphPointClamp        = v; return; }
    if (_stricmp(key, "PATCH_FACTORY_DUMP_SCAN") == 0)         { g_settings.patchFactoryDumpScan        = v; return; }
    if (_stricmp(key, "PATCH_PROD_LIST_VISIBILITY") == 0)      { g_settings.patchProdListVisibility     = v; return; }
    if (_stricmp(key, "PATCH_PROD_TYPE_GATE") == 0)            { g_settings.patchProdTypeGate           = v; return; }
    if (_stricmp(key, "PROD_TYPE_GATE_ALLOW_ALL") == 0)         { g_settings.prodTypeGateAllowAll        = v; return; }
    if (_stricmp(key, "PATCH_EXPONENTIAL_PRICE_DELTA") == 0)    { g_settings.patchExponentialPriceDelta  = v; return; }
    if (_stricmp(key, "PATCH_COMBAT_ROLL") == 0)                { g_settings.patchCombatRoll             = v; return; }
    if (_stricmp(key, "PATCH_CHECKSUM_DIAGNOSTIC") == 0)        { g_settings.patchChecksumDiagnostic     = v; return; }
    if (_stricmp(key, "ENABLE_OOS_LOG") == 0)                   { g_settings.enableOosLog                = v; return; }
    if (_stricmp(key, "ENABLE_CRASH_LOG") == 0)                 { g_settings.enableCrashLog              = v; return; }

    if (_stricmp(key, "PATCH_FPU_FORTRESS") == 0)               { g_settings.patchFpuFortress            = v; return; }
    if (_stricmp(key, "PATCH_D3D_FPU_PRESERVE") == 0)           { g_settings.patchD3dFpuPreserve         = v; return; }
    if (_stricmp(key, "PATCH_HEAP_LFH") == 0)                   { g_settings.patchHeapLfh                = v; return; }
    if (_stricmp(key, "PATCH_THREAD_FPU_PIN") == 0)             { g_settings.patchThreadFpuPin           = v; return; }
    if (_stricmp(key, "ENGINE_WORKER_THREADS") == 0)
    {
        g_settings.engineWorkerThreads = atoi(value);
        if (g_settings.engineWorkerThreads < 0)
            g_settings.engineWorkerThreads = 0;
        if (g_settings.engineWorkerThreads > 32)
            g_settings.engineWorkerThreads = 32;
        return;
    }
    if (_stricmp(key, "PATCH_POP_QUANTIZE") == 0)               { g_settings.patchPopQuantize            = v; return; }
    if (_stricmp(key, "POP_QUANTIZE_KEEP_BITS") == 0)
    {
        g_settings.popQuantizeKeepBits = atoi(value);
        return;
    }
    if (_stricmp(key, "PATCH_MP_CLIENT_SLEEP") == 0)            { g_settings.patchMpClientSleep          = v; return; }
    if (_stricmp(key, "MP_CLIENT_SLEEP_MS") == 0)
    {
        g_settings.mpClientSleepMs = atoi(value);
        return;
    }
    if (_stricmp(key, "PATCH_MAIN_LOOP_SLEEP0") == 0)           { g_settings.patchMainLoopSleep0         = v; return; }
    if (_stricmp(key, "MAIN_LOOP_SLEEP_MS") == 0)
    {
        g_settings.mainLoopSleepMs = atoi(value);
        return;
    }
    if (_stricmp(key, "PATCH_D3D_NO_VSYNC") == 0)               { g_settings.patchD3dNoVsync             = v; return; }
    if (_stricmp(key, "PATCH_HIGH_PRIORITY") == 0)              { g_settings.patchHighPriority           = v; return; }

    if (_stricmp(key, "COMBAT_ROLL_MIN") == 0) { g_settings.combatRollMin = atoi(value); return; }
    if (_stricmp(key, "COMBAT_ROLL_MAX") == 0) { g_settings.combatRollMax = atoi(value); return; }

    if (_stricmp(key, "PROD_TYPE_GATE_EXTRA_WHITELIST") == 0) { ParseExtraWhitelist(value); return; }

    // Пять частей одного патча (числитель/знаменатель формулы доли
    // аристократов) должны применяться только вместе - один ключ
    // на все 5 записей таблицы, а не по одной.
    if (_stricmp(key, "PATCH_ARISTOCRAT_INCOME_SHARE") == 0)
    {
        static const char* const names[] =
        {
            "aristocrat_income_share_patch_1",
            "aristocrat_income_share_patch_2",
            "aristocrat_income_share_patch_3",
            "aristocrat_income_share_patch_4",
            "aristocrat_income_share_patch_5",
        };
        for (int n = 0; n < 5; ++n)
            for (int i = 0; i < EXE_PATCH_COUNT; ++i)
                if (_stricmp(EXE_PATCHES[i].name, names[n]) == 0)
                    EXE_PATCHES[i].enabled = v;
        return;
    }

    if (_strnicmp(key, "PATCH_", 6) == 0)
    {
        const char* patchName = key + 6;
        for (int i = 0; i < EXE_PATCH_COUNT; ++i)
        {
            if (_stricmp(EXE_PATCHES[i].name, patchName) == 0)
            {
                EXE_PATCHES[i].enabled = v;
                return;
            }
        }
    }
}

// Ищет запись в EXE_PATCHES по имени - используется здесь, чтобы
// печатать записи таблицы в порядке категорий (Military/Economic/
// UI/Miscellaneous), а не в порядке объявления в таблице.
static bool FindExePatchEnabled(const char* name)
{
    for (int i = 0; i < EXE_PATCH_COUNT; ++i)
        if (_stricmp(EXE_PATCHES[i].name, name) == 0)
            return EXE_PATCHES[i].enabled;
    return false;
}

static void WriteDefaultSettings(const char* path)
{
    FILE* f = 0;
    if (fopen_s(&f, path, "w") != 0 || !f)
        return;

    fprintf(f,
        "PATCH_ALWAYS_ADD_WARGOALS=%d\n"
        "PATCH_LAND_REINFORCE=%d\n"
        "PATCH_NAVAL_REINFORCE=%d\n"
        "PATCH_ALLIED_REINFORCE_150=%d\n"
        "PATCH_OCCUPIED_REINFORCE_SPLIT=%d\n"
        "PATCH_ALLY_OWNER_CHECK=%d\n"
        "PATCH_COMBAT_ROLL=%d\n"
        "COMBAT_ROLL_MIN=%d\n"
        "COMBAT_ROLL_MAX=%d\n"
        "\n",
        (int)FindExePatchEnabled("always_add_wargoals"),
        (int)FindExePatchEnabled("land_reinforce"),
        (int)FindExePatchEnabled("naval_reinforce"),
        (int)FindExePatchEnabled("allied_reinforce_150"),
        (int)g_settings.patchOccupiedReinforceSplit,
        (int)g_settings.patchAllyOwnerCheck,
        (int)g_settings.patchCombatRoll,
        g_settings.combatRollMin,
        g_settings.combatRollMax);

    char extraWhitelistJoined[MAX_EXTRA_WHITELIST * EXTRA_WHITELIST_NAME_MAX] = "";
    for (int w = 0; w < g_extraWhitelistCount; ++w)
    {
        if (w > 0)
            strcat_s(extraWhitelistJoined, sizeof(extraWhitelistJoined), ",");
        strcat_s(extraWhitelistJoined, sizeof(extraWhitelistJoined), g_extraWhitelistNames[w]);
    }

    fprintf(f,
        "ENABLE_PRICE_DELTA=%d\n"
        "PATCH_EXPONENTIAL_PRICE_DELTA=%d\n"
        "PATCH_MAX_RELATIVE_PRICE=%d\n"
        "PATCH_BUILD_FACTORY_IGNORE_COLONIAL_1=%d\n"
        "PATCH_BUILD_FACTORY_IGNORE_COLONIAL_2=%d\n"
        "PATCH_BUILD_FACTORY_BUTTON_ENABLE_IGNORE_COLONIAL=%d\n"
        "PATCH_LOCAL_SUPPLY_FACTORY_IGNORE_COLONIAL=%d\n"
        "PATCH_BUILD_FACTORY_IGNORE_UNCIVILIZED_BUTTON=%d\n"
        "PATCH_BUILD_FACTORY_CHECKLIST_UNCIVILIZED_OWN=%d\n"
        "PATCH_BUILD_FACTORY_CHECKLIST_UNCIVILIZED_OTHER=%d\n"
        "PATCH_BUILD_FACTORY_IGNORE_UNCIVILIZED_CAN_BUILD=%d\n"
        "PATCH_PROD_TYPE_GATE=%d\n"
        "PROD_TYPE_GATE_ALLOW_ALL=%d\n"
        "PROD_TYPE_GATE_EXTRA_WHITELIST=%s\n"
        "\n",
        (int)g_settings.priceDelta,
        (int)g_settings.patchExponentialPriceDelta,
        (int)FindExePatchEnabled("max_relative_price"),
        (int)FindExePatchEnabled("build_factory_ignore_colonial_1"),
        (int)FindExePatchEnabled("build_factory_ignore_colonial_2"),
        (int)FindExePatchEnabled("build_factory_button_enable_ignore_colonial"),
        (int)FindExePatchEnabled("local_supply_factory_ignore_colonial"),
        (int)FindExePatchEnabled("build_factory_ignore_uncivilized_button"),
        (int)FindExePatchEnabled("build_factory_checklist_uncivilized_own"),
        (int)FindExePatchEnabled("build_factory_checklist_uncivilized_other"),
        (int)FindExePatchEnabled("build_factory_ignore_uncivilized_can_build"),
        (int)g_settings.patchProdTypeGate,
        (int)g_settings.prodTypeGateAllowAll,
        extraWhitelistJoined);

    fprintf(f,
        "ENABLE_BUTTONS=%d\n"
        "ENABLE_DECISION_FILTER=%d\n"
        "ENABLE_POP_DISPLAY=%d\n"
        "ENABLE_VERSION_LABEL=%d\n"
        "PATCH_PROD_LIST_VISIBILITY=%d\n"
        "\n",
        (int)g_settings.buttons,
        (int)g_settings.decisionFilter,
        (int)g_settings.popDisplay,
        (int)g_settings.versionLabel,
        (int)g_settings.patchProdListVisibility);

    fprintf(f,
        "PATCH_CONSCIOUSNESS_PLURALITY_GROWTH=%d\n"
        "PATCH_CIVILIZE_NULL_CHECK=%d\n"
        "PATCH_NULL_VTABLE_UI=%d\n"
        "PATCH_GRAPH_POINT_CLAMP=%d\n"
        "PATCH_CHECKSUM_DRIFT_FIX=%d\n"
        "PATCH_ALLOW_UNCIV_TECH_RESEARCH=%d\n"
        "PATCH_ARISTOCRAT_INCOME_SHARE=%d\n"
        "\n",
        (int)FindExePatchEnabled("consciousness_plurality_growth"),
        (int)g_settings.patchCivilizeNullCheck,
        (int)g_settings.patchNullVtableUi,
        (int)g_settings.patchGraphPointClamp,
        (int)FindExePatchEnabled("checksum_drift_fix"),
        (int)FindExePatchEnabled("allow_unciv_tech_research"),
        (int)FindExePatchEnabled("aristocrat_income_share_patch_1"));

    fprintf(f,
        "ENABLE_LOG=%d\n"
        "PATCH_FACTORY_DUMP_SCAN=%d\n"
        "PATCH_CHECKSUM_DIAGNOSTIC=%d\n"
        "ENABLE_OOS_LOG=%d\n"
        "ENABLE_CRASH_LOG=%d\n"
        "\n"
        "PATCH_FPU_FORTRESS=%d\n"
        "PATCH_D3D_FPU_PRESERVE=%d\n"
        "PATCH_HEAP_LFH=%d\n"
        "PATCH_THREAD_FPU_PIN=%d\n"
        "ENGINE_WORKER_THREADS=%d\n"
        "PATCH_POP_QUANTIZE=%d\n"
        "POP_QUANTIZE_KEEP_BITS=%d\n"
        "PATCH_MP_CLIENT_SLEEP=%d\n"
        "MP_CLIENT_SLEEP_MS=%d\n"
        "PATCH_MAIN_LOOP_SLEEP0=%d\n"
        "MAIN_LOOP_SLEEP_MS=%d\n"
        "PATCH_D3D_NO_VSYNC=%d\n"
        "PATCH_HIGH_PRIORITY=%d\n",
        (int)g_settings.log,
        (int)g_settings.patchFactoryDumpScan,
        (int)g_settings.patchChecksumDiagnostic,
        (int)g_settings.enableOosLog,
        (int)g_settings.enableCrashLog,
        (int)g_settings.patchFpuFortress,
        (int)g_settings.patchD3dFpuPreserve,
        (int)g_settings.patchHeapLfh,
        (int)g_settings.patchThreadFpuPin,
        g_settings.engineWorkerThreads,
        (int)g_settings.patchPopQuantize,
        g_settings.popQuantizeKeepBits,
        (int)g_settings.patchMpClientSleep,
        g_settings.mpClientSleepMs,
        (int)g_settings.patchMainLoopSleep0,
        g_settings.mainLoopSleepMs,
        (int)g_settings.patchD3dNoVsync,
        (int)g_settings.patchHighPriority);

    fclose(f);
}

// Формат строки: KEY=VALUE, необязательный "; комментарий" в
// хвосте строки не мешает разбору (atoi останавливается на первой
// нецифровой позиции). Строки без '=' (пустые, комментарии) пропускаются.
static void LoadSettings()
{
    static const char* PATH = "v2dll_settings.ini";

    FILE* f = 0;
    if (fopen_s(&f, PATH, "r") != 0 || !f)
    {
        WriteDefaultSettings(PATH);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char* eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        const char* value = eq + 1;

        char key[64];
        size_t klen = strlen(line);
        if (klen >= sizeof(key))
            klen = sizeof(key) - 1;
        memcpy(key, line, klen);
        key[klen] = '\0';
        while (klen > 0 && (key[klen - 1] == ' ' || key[klen - 1] == '\t'))
            key[--klen] = '\0';

        ApplySetting(key, value);
    }

    fclose(f);
}


static DWORD FileOffsetToRVA(DWORD fileOffset)
{
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;

    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
    {
        DWORD start = sec->PointerToRawData;
        DWORD size = sec->SizeOfRawData;

        if (fileOffset >= start && fileOffset < start + size)
            return sec->VirtualAddress + (fileOffset - start);
    }

    return 0;
}


static void InstallExePatches()
{
    for (int i = 0; i < EXE_PATCH_COUNT; ++i)
    {
        BytePatch& bp = EXE_PATCHES[i];

        if (!bp.enabled)
            continue;

        DWORD rva = bp.rva ? bp.rva : FileOffsetToRVA(bp.fileOffset);
        if (!rva)
        {
            Log("Patch '%s': смещение %06X вне секций", bp.name, bp.fileOffset);
            continue;
        }

        unsigned char* at = (unsigned char*)(g_base + rva);

        if (memcmp(at, bp.expect, bp.len) != 0)
        {
            Log("Patch '%s': сигнатура не совпала - не патчим", bp.name);
            continue;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(at, bp.len, PAGE_EXECUTE_READWRITE, &oldProtect))
            continue;

        memcpy(at, bp.replace, bp.len);
        VirtualProtect(at, bp.len, oldProtect, &oldProtect);

        Log("Patch '%s': применён на rva %06X", bp.name, rva);
    }
}


// ---------------------------------------------------------------
// Разделение ставки пополнения: occupied vs allied
//
// allied_reinforce_150 выше правит ЕДИНСТВЕННУЮ инструкцию
// mov eax,1000 на LAB_005d74bb (FUN_005d7420) — но в эту точку
// сходятся ДВЕ разные ветки: "мы сами контролируем провинцию,
// не являющуюся нашим кором" (occupied) и "провинция под контролем
// союзника, не в состоянии войны" (allied). Общий mov затрагивает
// обе сразу, разделить его без ветвления нельзя (см. коммент выше).
//
// Перехватываем ветку occupied ДО слияния: "cmp edx,[ecx+0x20];
// jz LAB_005d74bb" (rva 0x1D751B, 5 байт) — это и есть проверка
// "владелец юнита == страна, контролирующая провинцию". Прыгаем
// в пещеру, повторяем cmp; при совпадении (occupied) пишем свою
// ставку и уходим в общий эпилог функции (rva 0x1D74C5,
// mov eax,ecx; pop edi; pop esi; pop ebx; mov esp,ebp; pop ebp;
// ret 4) — он лежит ПОСЛЕ патчуемой allied_reinforce_150
// инструкции и ею не затронут. При несовпадении (allied)
// повторяем перекрытую "mov esi,[ecx+0xbe8]" и прыгаем обратно
// в оригинальный код (rva 0x1D7526) — та ветка идёт дальше как
// раньше и сама попадает на LAB_005d74bb, где её по-прежнему
// ждут патченные 1500 (150.0%) от allied_reinforce_150.
// ---------------------------------------------------------------

static const int OCCUPIED_REINFORCE_RATE = 1000;   // 100.0%, как в ванили

static const DWORD RVA_OCC_REINFORCE_HOOK   = 0x1D751B;   // cmp edx,[ecx+0x20]; jz
static const DWORD RVA_OCC_REINFORCE_RESUME = 0x1D7526;   // mov edx,[esi+edx*4] (продолжение allied-ветки)
static const DWORD RVA_OCC_REINFORCE_TAIL   = 0x1D74C5;   // mov eax,ecx; pop edi; ...; ret 4
static const DWORD RVA_LAB_74BB             = 0x1D74BB;   // mov ecx,[ebp+8]; mov eax,1000(->1500) — LAB_005d74bb

static const unsigned char OCC_REINFORCE_SIG[5] =
{ 0x3B, 0x51, 0x20, 0x74, 0x9B };

static bool InstallOccupiedReinforceSplit()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_OCC_REINFORCE_HOOK);

    if (memcmp(hook, OCC_REINFORCE_SIG, sizeof(OCC_REINFORCE_SIG)) != 0)
    {
        Log("OccupiedReinforceSplit: сигнатура не совпала - не патчим");
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
        return false;

    int n = 0;

    // Повторяем перекрытые прыжком байты.
    cave[n++] = 0x3B; cave[n++] = 0x51; cave[n++] = 0x20;   // cmp edx,[ecx+0x20]

    int jzAt = n;
    cave[n++] = 0x74; cave[n++] = 0x00;                     // jz occupied (адрес допишем ниже)

    // allied: восстанавливаем перекрытую "mov esi,[ecx+0xbe8]"
    // и уходим обратно в оригинальный код.
    cave[n++] = 0x8B; cave[n++] = 0xB1;
    *(DWORD*)(cave + n) = 0xBE8; n += 4;

    cave[n++] = 0xE9;                                       // jmp обратно (allied продолжается как раньше)
    *(DWORD*)(cave + n) = (g_base + RVA_OCC_REINFORCE_RESUME) - (DWORD)(cave + n + 4);
    n += 4;

    int occupiedAt = n;
    cave[jzAt + 1] = (unsigned char)(occupiedAt - (jzAt + 2));

    cave[n++] = 0x8B; cave[n++] = 0x4D; cave[n++] = 0x08;   // mov ecx,[ebp+8]
    cave[n++] = 0xB8;                                       // mov eax, OCCUPIED_REINFORCE_RATE
    *(DWORD*)(cave + n) = (DWORD)OCCUPIED_REINFORCE_RATE; n += 4;
    cave[n++] = 0x89; cave[n++] = 0x01;                     // mov [ecx],eax

    cave[n++] = 0xE9;                                       // jmp в общий эпилог функции
    *(DWORD*)(cave + n) = (g_base + RVA_OCC_REINFORCE_TAIL) - (DWORD)(cave + n + 4);
    n += 4;

    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)cave - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("OccupiedReinforceSplit: occupied=%d.%d%%, пещера %08X",
        OCCUPIED_REINFORCE_RATE / 10, OCCUPIED_REINFORCE_RATE % 10, (DWORD)(DWORD_PTR)cave);
    return true;
}


// ---------------------------------------------------------------
// Разделение ставки пополнения: occupied-by-ally vs owned-by-ally
//
// InstallOccupiedReinforceSplit выше ловит только случай "мы сами
// контролируем провинцию не по кору" (Check A, cmp edx,[ecx+0x20]).
// Но в ту же ветку LAB_005d74bb ведёт и Check B — "провинция под
// контролем страны, дружественной нам" (relations[controller][us]
// == 0), — а Check B срабатывает НЕЗАВИСИМО от того, владеет ли
// эта дружественная страна провинцией по кору или тоже просто
// оккупировала её. То есть "союзник владеет" и "союзник оккупирует
// чужую территорию" сейчас неотличимы и обе идут на 150%.
//
// Три JNZ на rva 0x1D7544/0x1D754E/0x1D7558 (проверка тега
// контроллера на "REB" побайтово) при непопадании ведут именно на
// LAB_005d74bb — это единственные точки входа в Check B. Все три
// уже закодированы компилятором как near jmp (0F 85 rel32, 6 байт,
// т.к. LAB_005d74bb не достаётся коротким прыжком отсюда), поэтому
// правим только 4-байтовый rel32, без пещеры-трамплина для самих
// прыжков — только для новой развилки, куда они теперь ведут.
//
// В развилке провинция (EDI, не меняется по всей функции) даёт
// owner (+0x12c) и controller (+0x134) напрямую, без опоры на ECX/
// EDX, которые Check B успел затереть под свои нужды: если они
// равны — контроллер и есть настоящий владелец, это owned-by-ally
// (150%, уходим на исходный LAB_005d74bb); если нет — оккупация
// союзником чужой земли (100%, тот же хвост-эпилог, что и у Check A).
// ---------------------------------------------------------------

static const DWORD RVA_ALLY_JNZ1 = 0x1D7544;   // jnz LAB_005d74bb (controller.tag[0] != 'R')
static const DWORD RVA_ALLY_JNZ2 = 0x1D754E;   // jnz LAB_005d74bb (controller.tag[1] != 'E')
static const DWORD RVA_ALLY_JNZ3 = 0x1D7558;   // jnz LAB_005d74bb (controller.tag[2] != 'B')

static const unsigned char ALLY_JNZ1_SIG[6] = { 0x0F, 0x85, 0x71, 0xFF, 0xFF, 0xFF };
static const unsigned char ALLY_JNZ2_SIG[6] = { 0x0F, 0x85, 0x67, 0xFF, 0xFF, 0xFF };
static const unsigned char ALLY_JNZ3_SIG[6] = { 0x0F, 0x85, 0x5D, 0xFF, 0xFF, 0xFF };

static bool RepointNearJnz(DWORD rva, const unsigned char* sig, DWORD newTargetVA)
{
    unsigned char* at = (unsigned char*)(g_base + rva);

    if (memcmp(at, sig, 6) != 0)
    {
        Log("AllyOwnerCheck: сигнатура не совпала на rva %06X - не патчим", rva);
        return false;
    }

    unsigned char patch[6];
    patch[0] = 0x0F;
    patch[1] = 0x85;
    *(DWORD*)(patch + 2) = newTargetVA - ((DWORD)at + 6);

    DWORD oldProtect = 0;
    if (!VirtualProtect(at, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(at, patch, sizeof(patch));
    VirtualProtect(at, sizeof(patch), oldProtect, &oldProtect);
    return true;
}

static bool InstallAllyOwnerCheck()
{
    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
        return false;

    int n = 0;

    cave[n++] = 0x8B; cave[n++] = 0x97;                     // mov edx,[edi+0x12c]  (province.owner_id)
    *(DWORD*)(cave + n) = 0x12C; n += 4;

    cave[n++] = 0x3B; cave[n++] = 0x97;                     // cmp edx,[edi+0x134]  (province.controller_id)
    *(DWORD*)(cave + n) = 0x134; n += 4;

    cave[n++] = 0x0F; cave[n++] = 0x84;                     // jz owned-by-ally -> исходный LAB_005d74bb (150%)
    *(DWORD*)(cave + n) = (g_base + RVA_LAB_74BB) - (DWORD)(cave + n + 4);
    n += 4;

    // occupied-by-ally: своя ставка, тот же хвост-эпилог, что и у Check A.
    cave[n++] = 0x8B; cave[n++] = 0x4D; cave[n++] = 0x08;   // mov ecx,[ebp+8]
    cave[n++] = 0xB8;                                       // mov eax, OCCUPIED_REINFORCE_RATE
    *(DWORD*)(cave + n) = (DWORD)OCCUPIED_REINFORCE_RATE; n += 4;
    cave[n++] = 0x89; cave[n++] = 0x01;                     // mov [ecx],eax

    cave[n++] = 0xE9;                                       // jmp в общий эпилог функции
    *(DWORD*)(cave + n) = (g_base + RVA_OCC_REINFORCE_TAIL) - (DWORD)(cave + n + 4);
    n += 4;

    DWORD target = (DWORD)(DWORD_PTR)cave;

    bool ok = true;
    ok &= RepointNearJnz(RVA_ALLY_JNZ1, ALLY_JNZ1_SIG, target);
    ok &= RepointNearJnz(RVA_ALLY_JNZ2, ALLY_JNZ2_SIG, target);
    ok &= RepointNearJnz(RVA_ALLY_JNZ3, ALLY_JNZ3_SIG, target);

    Log("AllyOwnerCheck: owned-by-ally=150%%, occupied-by-ally=%d.%d%%, пещера %08X, ok=%d",
        OCCUPIED_REINFORCE_RATE / 10, OCCUPIED_REINFORCE_RATE % 10, target, (int)ok);
    return ok;
}


// ---------------------------------------------------------------
// Краш при цивилизации страны (0xc0000005, fault offset 0x14248b).
//
// FUN_00542370 - обработчик "on_civilize": перебирает существующие
// постройки страны (FUN_005c2ad0 - lookup по имени в хэш-таблице
// "категория/товар -> слот") и раскладывает их по слотам для
// последующей обработки. Раньше это падало только теоретически:
// нецивилизованные страны не могли строить фабрики вообще, поэтому
// до этого кода просто не доходило ни с чем, кроме RGO-построек,
// для которых слот всегда существует. В этой сессии добавлены
// патчи build_factory_ignore_uncivilized_* — теперь нецивилизованная
// страна МОЖЕТ построить произвольную фабрику, и при её цивилизации
// FUN_005c2ad0 не находит слот для такой постройки, возвращает 0
// (не найдено), а вызывающий код разыменовывает результат без
// проверки: mov esi,[eax+0x40] - EAX=0 -> чтение по 0x40 -> краш.
//
// Патчим ровно точку сразу после call FUN_005c2ad0 (rva 0x14248B,
// 7 байт - перекрывает mov esi,[eax+0x40]; dec esi; shl esi,4).
// Если EAX==0 - пропускаем текущую постройку целиком (прыжок на
// rva 0x142555, штатная точка "next iteration" того же цикла,
// уже присутствующая в оригинальном коде). Иначе - воспроизводим
// три перекрытых инструкции и продолжаем как раньше (rva 0x142492).
// ---------------------------------------------------------------

static const DWORD RVA_CIVILIZE_NULLCHECK_HOOK   = 0x14248B;
static const DWORD RVA_CIVILIZE_NULLCHECK_NORMAL = 0x142492;  // add esi,[ebp-0x48]
static const DWORD RVA_CIVILIZE_NULLCHECK_SKIP   = 0x142555;  // mov eax,[ebp-0x18] (next iteration)

static const unsigned char CIVILIZE_NULLCHECK_SIG[7] =
{ 0x8B, 0x70, 0x40, 0x4E, 0xC1, 0xE6, 0x04 };

static bool InstallCivilizeNullCheck()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_CIVILIZE_NULLCHECK_HOOK);

    if (memcmp(hook, CIVILIZE_NULLCHECK_SIG, sizeof(CIVILIZE_NULLCHECK_SIG)) != 0)
    {
        Log("CivilizeNullCheck: сигнатура не совпала - не патчим");
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
        return false;

    int n = 0;

    cave[n++] = 0x85; cave[n++] = 0xC0;                     // test eax,eax
    int jzAt = n;
    cave[n++] = 0x74; cave[n++] = 0x00;                     // jz null_case (адрес допишем ниже)

    // EAX != 0: воспроизводим перекрытые байты и уходим обратно.
    cave[n++] = 0x8B; cave[n++] = 0x70; cave[n++] = 0x40;   // mov esi,[eax+0x40]
    cave[n++] = 0x4E;                                       // dec esi
    cave[n++] = 0xC1; cave[n++] = 0xE6; cave[n++] = 0x04;   // shl esi,4

    cave[n++] = 0xE9;                                       // jmp обратно (нормальный путь)
    *(DWORD*)(cave + n) = (g_base + RVA_CIVILIZE_NULLCHECK_NORMAL) - (DWORD)(cave + n + 4);
    n += 4;

    int nullAt = n;
    cave[jzAt + 1] = (unsigned char)(nullAt - (jzAt + 2));

    // EAX == 0: слот не найден - пропускаем эту постройку целиком,
    // на следующую итерацию того же цикла (штатная точка выхода).
    cave[n++] = 0xE9;
    *(DWORD*)(cave + n) = (g_base + RVA_CIVILIZE_NULLCHECK_SKIP) - (DWORD)(cave + n + 4);
    n += 4;

    unsigned char patch[7];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)cave - ((DWORD)hook + 5);
    patch[5] = 0x90;
    patch[6] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("CivilizeNullCheck: установлен на rva %06X, пещера %08X",
        RVA_CIVILIZE_NULLCHECK_HOOK, (DWORD)(DWORD_PTR)cave);
    return true;
}


// ---------------------------------------------------------------
// Краш UI-строки после OOS-диалога (0xc0000005, fault rva 0x4A8E0B).
//
// FUN_008A8DC0 подбирает текстовое поле: несколько fallback'ов
// (объект+0x14, потом +0x18, индекс через session+0xACC). На первом
// уже есть проверка «указатель == 0 → следующий fallback», но нет
// проверки vtable. После разборки OOS-диалога объект ещё жив, а
// vtable уже NULL → mov eax,[edx+0x20] читает 0x20 и падает.
// lua51 на стеке не было: это ванильный exe, не наш хук.
//
// Патч только UI: если vtable или слот +0x20 нулевые — тот же
// штатный переход на следующий fallback (rva 0x4A8E31), что и при
// пустом указателе / ложном virtual-call. Симуляция и checksum
// не затрагиваются, MP-безопасно.
//
// Сигнатура 16 байт с уникальным je +0x2B: голые 8B 11 8B 42 20 FF D0
// встречаются в exe десятки раз.
// ---------------------------------------------------------------

static const DWORD RVA_NULL_VTABLE_UI_SIG    = 0x4A8E00;
static const DWORD RVA_NULL_VTABLE_UI_HOOK   = 0x4A8E09;
static const DWORD RVA_NULL_VTABLE_UI_NORMAL = 0x4A8E10;  // test al,al
static const DWORD RVA_NULL_VTABLE_UI_SKIP   = 0x4A8E31;  // next fallback

static const unsigned char NULL_VTABLE_UI_SIG[16] =
{
    0x83, 0x78, 0x14, 0x00,             // cmp dword [eax+0x14], 0
    0x74, 0x2B,                         // je  rva 0x4A8E31
    0x8B, 0x48, 0x14,                   // mov ecx, [eax+0x14]
    0x8B, 0x11,                         // mov edx, [ecx]
    0x8B, 0x42, 0x20,                   // mov eax, [edx+0x20]
    0xFF, 0xD0                          // call eax
};

static bool InstallNullVtableUi()
{
    unsigned char* sig = (unsigned char*)(g_base + RVA_NULL_VTABLE_UI_SIG);
    unsigned char* hook = (unsigned char*)(g_base + RVA_NULL_VTABLE_UI_HOOK);

    if (memcmp(sig, NULL_VTABLE_UI_SIG, sizeof(NULL_VTABLE_UI_SIG)) != 0)
    {
        Log("NullVtableUi: сигнатура не совпала - не патчим");
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 48, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave)
        return false;

    int n = 0;
    cave[n++] = 0x8B; cave[n++] = 0x11;                 // mov edx,[ecx]
    cave[n++] = 0x85; cave[n++] = 0xD2;                 // test edx,edx
    int jzVt = n;
    cave[n++] = 0x74; cave[n++] = 0x00;                 // jz skip
    cave[n++] = 0x8B; cave[n++] = 0x42; cave[n++] = 0x20; // mov eax,[edx+0x20]
    cave[n++] = 0x85; cave[n++] = 0xC0;                 // test eax,eax
    int jzFn = n;
    cave[n++] = 0x74; cave[n++] = 0x00;                 // jz skip
    cave[n++] = 0xFF; cave[n++] = 0xD0;                 // call eax
    cave[n++] = 0xE9;                                   // jmp test al,al
    *(DWORD*)(cave + n) = (g_base + RVA_NULL_VTABLE_UI_NORMAL) - (DWORD)(cave + n + 4);
    n += 4;

    int skipAt = n;
    cave[jzVt + 1] = (unsigned char)(skipAt - (jzVt + 2));
    cave[jzFn + 1] = (unsigned char)(skipAt - (jzFn + 2));

    cave[n++] = 0xE9;
    *(DWORD*)(cave + n) = (g_base + RVA_NULL_VTABLE_UI_SKIP) - (DWORD)(cave + n + 4);
    n += 4;

    unsigned char patch[7];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)cave - ((DWORD)hook + 5);
    patch[5] = 0x90;
    patch[6] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("NullVtableUi: установлен на rva %06X, пещера %08X",
        RVA_NULL_VTABLE_UI_HOOK, (DWORD)(DWORD_PTR)cave);
    return true;
}


// ---------------------------------------------------------------
// Второй UI-краш после OOS (хост, 2.98): ACCESS_VIOLATION на
// rva 0x1D0F09 / FUN_005D0EB0. Один caller (FUN_005DF4B0 rva
// 0x1DF56B) — цикл identity/localised-string через session+0xACC.
// На стеке краша: "identity" "synchronous" "MONARCHTITLE"/"TITLE".
// Это отрисовка подписи, не симуляция.
//
// esi жив, но [esi] — не vtable exe, а висячий heap (B1EB3650).
// FUN_005AB7F0 (кэш identity в this+0x3C) вернул этот указатель,
// caller сразу делает mov esi,eax и зовёт виртуальные методы.
// Штатный выход xor al,al; ret 4 (rva 0x1D0F3F) — тот же,
// что при [esi+0x40] < 1. Caller делает test al / jz next item.
//
// Причина, которую можно закрыть из DLL: не кормить identity
// мёртвым узлом и не оставлять его в кэше +0x3C. Сам пул
// (кто освободил узел, пока армия ещё держит id) — граф
// объектов движка, его из прокси не чинят без риска MP.
// ---------------------------------------------------------------

static const DWORD RVA_IDENT_VT_SIG    = 0x1D0EFF;
static const DWORD RVA_IDENT_VT_HOOK   = 0x1D0F07;
static const DWORD RVA_IDENT_VT_RESUME = 0x1D0F14;  // test al,al
static const DWORD RVA_IDENT_VT_SKIP   = 0x1D0F3E;  // xor al,al; epilogue

static const unsigned char IDENT_VT_SIG[16] =
{
    0x8B, 0x46, 0x40,                   // mov eax, [esi+0x40]
    0x83, 0xF8, 0x01,                   // cmp eax, 1
    0x7C, 0x37,                         // jl  rva 0x1D0F3F
    0x8B, 0x06,                         // mov eax, [esi]
    0x8B, 0x90, 0x88, 0x00, 0x00, 0x00  // mov edx, [eax+0x88]
};

static DWORD g_identVtResume = 0;
static DWORD g_identVtSkip = 0;
static DWORD g_identLookupResume = 0;
static DWORD g_identLookupSkip = 0;

// Диагностика OOS→UAF (3.02): только счётчики, симуляцию не трогают.
// Хост часто не видит DIFF (oos_hits=0) — тогда first_real остаётся "-"
// и days_real=-1, а ident_skip на SYNC-строках всё равно растёт.
static LONG g_identSkipVt = 0;
static LONG g_identSkipLookup = 0;
static LONG g_identSkipSlot84 = 0;
static LONG g_identSkipE4 = 0;
static LONG g_identSkipFn = 0;
static LONG g_identSkipFn2 = 0;
static LONG g_identSkipFn3 = 0;
static LONG g_identSkipStr = 0;
static LONG g_identSkipLogged = 0;
static char g_lastIdentSkipLine[192] = "-";
static int  g_lastDateRaw = 0;
static char g_lastDateBuf[32] = "-";
static int  g_firstOosRaw = 0;
static char g_firstOosBuf[32] = "-";
static int  g_firstRealOosRaw = 0;
static char g_firstRealOosBuf[32] = "-";

static void LogOosFile(const char* fmt, ...);

static int DaysSinceFirstRealOos()
{
    if (!g_firstRealOosRaw || !g_lastDateRaw)
        return -1;
    int d = (g_lastDateRaw - g_firstRealOosRaw) / 24;
    return d < 0 ? 0 : d;
}

static void RememberSessionClock(int raw, const char* buf)
{
    if (!raw || !buf || !buf[0])
        return;
    g_lastDateRaw = raw;
    strcpy_s(g_lastDateBuf, buf);
}

static LONG IdentSkipTotal()
{
    return g_identSkipVt + g_identSkipLookup + g_identSkipSlot84 +
        g_identSkipE4 + g_identSkipFn + g_identSkipFn2 + g_identSkipFn3 +
        g_identSkipStr;
}

static void FormatIdentDiag(char* buf, size_t cap)
{
    sprintf_s(buf, cap,
        "ident_skip vt=%d lookup=%d slot84=%d e4=%d fn=%d fn2=%d fn3=%d str=%d first_oos=%s first_real=%s days_real=%d last_skip=%s",
        (int)g_identSkipVt, (int)g_identSkipLookup, (int)g_identSkipSlot84,
        (int)g_identSkipE4, (int)g_identSkipFn, (int)g_identSkipFn2,
        (int)g_identSkipFn3, (int)g_identSkipStr,
        g_firstOosBuf, g_firstRealOosBuf,
        DaysSinceFirstRealOos(), g_lastIdentSkipLine);
}

static void __stdcall IdentNoteSkip(int site, void* self)
{
    LONG* counter = &g_identSkipVt;
    const char* siteName = "vtable";
    if (site == 1)
    {
        counter = &g_identSkipLookup;
        siteName = "lookup";
    }
    else if (site == 2)
    {
        counter = &g_identSkipSlot84;
        siteName = "slot84";
    }
    else if (site == 3)
    {
        counter = &g_identSkipE4;
        siteName = "fieldE4";
    }
    else if (site == 4)
    {
        counter = &g_identSkipFn;
        siteName = "fnD1BB0";
    }
    else if (site == 6)
    {
        counter = &g_identSkipFn2;
        siteName = "fnD4420";
    }
    else if (site == 7)
    {
        counter = &g_identSkipFn3;
        siteName = "fn113D50";
    }
    else if (site == 5)
    {
        counter = &g_identSkipStr;
        siteName = "strClear";
    }
    InterlockedIncrement(counter);
    LONG total = IdentSkipTotal();

    DWORD vptr = 0;
    DWORD id40 = 0;
    __try
    {
        vptr = *(DWORD*)self;
        id40 = *(DWORD*)((char*)self + 0x40);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        vptr = 0;
        id40 = 0;
    }

    _snprintf_s(g_lastIdentSkipLine, sizeof(g_lastIdentSkipLine), _TRUNCATE,
        "n=%d site=%s date=%s self=%08X vptr=%08X id40=%08X",
        (int)total, siteName, g_lastDateBuf,
        (unsigned)(DWORD_PTR)self, vptr, id40);

    if (total > 20 && (total % 50) != 0)
        return;
    if (!g_settings.enableOosLog)
        return;

    LogOosFile("IDENT skip %s days_real=%d first_real=%s",
        g_lastIdentSkipLine, DaysSinceFirstRealOos(), g_firstRealOosBuf);
}

static int __stdcall IdentVtableOk(void* self)
{
    if (!self)
        return 0;
    DWORD vptr = 0;
    DWORD fn = 0;
    __try
    {
        vptr = *(DWORD*)self;
        if (!vptr)
            return 0;
        // Живой C++-объект Vic2: vtable в образе exe (.rdata).
        // Куча / освобождённый буфер строки сюда не попадает — это и было
        // B1EB3650 на хосте (первый dword = висячий heap, не vtable).
        if (!g_base || vptr < g_base || vptr >= g_base + g_imageSize)
            return 0;
        fn = *(DWORD*)(vptr + 0x88);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    return fn ? 1 : 0;
}

static int __stdcall IdentVtableCheck(void* self, int site)
{
    if (IdentVtableOk(self))
        return 1;
    if (self)
        IdentNoteSkip(site, self);
    return 0;
}

__declspec(naked) static void IdentVtableCave()
{
    __asm {
        push 0
        push esi
        call IdentVtableCheck
        test eax, eax
        jz skip
        mov eax, dword ptr [esi]
        mov edx, dword ptr [eax + 0x88]
        push edi
        mov ecx, esi
        call edx
        jmp dword ptr [g_identVtResume]
    skip:
        jmp dword ptr [g_identVtSkip]
    }
}

static const DWORD RVA_IDENT_LOOKUP_SIG    = 0x1DF532;
static const DWORD RVA_IDENT_LOOKUP_RESUME = 0x1DF537;  // mov [esp+0x10], 0
static const DWORD RVA_IDENT_LOOKUP_SKIP   = 0x1DF581;  // pop edi; al=1; ret

static const unsigned char IDENT_LOOKUP_SIG[13] =
{
    0x8B, 0xF0,                         // mov esi, eax
    0xC1, 0xFB, 0x02,                   // sar ebx, 2
    0xC7, 0x44, 0x24, 0x10, 0x00, 0x00, 0x00, 0x00
};

__declspec(naked) static void IdentLookupCave()
{
    __asm {
        mov esi, eax
        sar ebx, 2
        push 1
        push esi
        call IdentVtableCheck
        test eax, eax
        jz bad
        jmp dword ptr [g_identLookupResume]
    bad:
        mov dword ptr [edi + 0x3C], 0
        xor esi, esi
        jmp dword ptr [g_identLookupSkip]
    }
}

static bool InstallIdentityLookupGuard()
{
    unsigned char* sig = (unsigned char*)(g_base + RVA_IDENT_LOOKUP_SIG);
    unsigned char* hook = sig;

    if (memcmp(sig, IDENT_LOOKUP_SIG, sizeof(IDENT_LOOKUP_SIG)) != 0)
    {
        Log("IdentityLookupGuard: сигнатура не совпала - не патчим");
        return false;
    }

    g_identLookupResume = g_base + RVA_IDENT_LOOKUP_RESUME;
    g_identLookupSkip = g_base + RVA_IDENT_LOOKUP_SKIP;

    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&IdentLookupCave - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("IdentityLookupGuard: установлен на rva %06X -> dll %08X",
        RVA_IDENT_LOOKUP_SIG, (DWORD)(DWORD_PTR)&IdentLookupCave);
    return true;
}

static bool InstallIdentityNullVtable()
{
    unsigned char* sig = (unsigned char*)(g_base + RVA_IDENT_VT_SIG);
    unsigned char* hook = (unsigned char*)(g_base + RVA_IDENT_VT_HOOK);

    if (memcmp(sig, IDENT_VT_SIG, sizeof(IDENT_VT_SIG)) != 0)
    {
        Log("IdentityNullVtable: сигнатура не совпала (%02X %02X %02X %02X %02X %02X %02X %02X) - не патчим",
            sig[0], sig[1], sig[2], sig[3], sig[4], sig[5], sig[6], sig[7]);
        return false;
    }

    g_identVtResume = g_base + RVA_IDENT_VT_RESUME;
    g_identVtSkip = g_base + RVA_IDENT_VT_SKIP;

    unsigned char patch[8];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&IdentVtableCave - ((DWORD)hook + 5);
    patch[5] = 0x90;
    patch[6] = 0x90;
    patch[7] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("IdentityNullVtable: установлен на rva %06X -> dll %08X",
        RVA_IDENT_VT_HOOK, (DWORD)(DWORD_PTR)&IdentVtableCave);
    return true;
}

// Тот же мёртвый identity, другой слот vtable (+0x84).
// Хост 3.02, 1914-06-23: lookup уже skip'нул 44384B90, через 31 мс
// FUN_005B9670 всё равно сделал mov eax,[vtable+0x84] и упал.
// Нет прямых E8 — виртуальный метод. Штатный отказ: xor eax,eax; ret 12
// (как setnz после неуспешного call).
static const DWORD RVA_IDENT_84_SIG = 0x1B9670;
static const unsigned char IDENT_84_SIG[16] =
{
    0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x08, 0x8B, 0x01,
    0x8B, 0x55, 0x10, 0x8B, 0x80, 0x84, 0x00, 0x00
};

__declspec(naked) static void IdentSlot84Cave()
{
    __asm {
        push ebp
        mov ebp, esp
        push 2
        push dword ptr [ebp + 8]
        call IdentVtableCheck
        test eax, eax
        jz fail
        mov ecx, dword ptr [ebp + 8]
        mov eax, dword ptr [ecx]
        mov edx, dword ptr [ebp + 0x10]
        mov eax, dword ptr [eax + 0x84]
        push edx
        call eax
        test al, al
        setnz al
        pop ebp
        ret 12
    fail:
        xor eax, eax
        pop ebp
        ret 12
    }
}

static bool InstallIdentitySlot84()
{
    unsigned char* sig = (unsigned char*)(g_base + RVA_IDENT_84_SIG);
    unsigned char* hook = sig;

    if (memcmp(sig, IDENT_84_SIG, sizeof(IDENT_84_SIG)) != 0)
    {
        Log("IdentitySlot84: сигнатура не совпала - не патчим");
        return false;
    }

    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&IdentSlot84Cave - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("IdentitySlot84: установлен на rva %06X -> dll %08X",
        RVA_IDENT_84_SIG, (DWORD)(DWORD_PTR)&IdentSlot84Cave);
    return true;
}

// Клиент 3.03, 1914-06-23: lookup+slot84 уже skip'нули esi=43610990,
// через 58 мс FUN_005D1BB0 всё равно делает
//   mov ecx, [esi+0xE4] ; cmp [eax], [ecx]
// На мёртвом узле +0xE4 — мелкое целое (00001D6F), не указатель.
// Штатный «не совпало» — xorps xmm0 (rva 0x1D1CD5), но дальше
// функция всё равно пишет [esi+0xF8]/[+0x100]/[+0xE4] и зовёт
// аллокатор. На мёртвом this это куча без лога (клиент 3.05:
// fieldE4 skip, crash.log пуст). Прыгаем на эпилог SEH
// (rva 0x1D1E49, ret 8) — тот же выход, что jz после пустого
// списка в хвосте функции.
//
// 3.04 на запуске: IdentVtableCheck оставляет eax=1, resume
// `mov edx,[eax]` читает 00000001. Живой this (CNavy) сюда тоже
// заходит — eax ДО вызова надо сохранить.
static const DWORD RVA_IDENT_E4_SIG    = 0x1D1CC9;
static const DWORD RVA_IDENT_E4_HOOK   = 0x1D1CC9;
static const DWORD RVA_IDENT_E4_RESUME = 0x1D1CCF;  // mov edx,[eax]
static const DWORD RVA_IDENT_E4_SKIP   = 0x1D1E49;  // mov ecx,[ebp-0xC]; pop edi/esi; ret 8
static const unsigned char IDENT_E4_EPI[8] =
{
    0x8B, 0x4D, 0xF4, 0x5F, 0x5E, 0x64, 0x89, 0x0D
};

static const unsigned char IDENT_E4_SIG[12] =
{
    0x8B, 0x8E, 0xE4, 0x00, 0x00, 0x00, // mov ecx, [esi+0xE4]
    0x8B, 0x10,                         // mov edx, [eax]
    0x3B, 0x11,                         // cmp edx, [ecx]
    0x74, 0x3E
};

static DWORD g_identE4Resume = 0;
static DWORD g_identE4Skip = 0;

__declspec(naked) static void IdentFieldE4Cave()
{
    __asm {
        push eax
        push 3
        push esi
        call IdentVtableCheck
        test eax, eax
        jz skip
        pop eax
        mov ecx, dword ptr [esi + 0xE4]
        jmp dword ptr [g_identE4Resume]
    skip:
        pop eax
        jmp dword ptr [g_identE4Skip]
    }
}

static bool InstallIdentityFieldE4()
{
    unsigned char* sig = (unsigned char*)(g_base + RVA_IDENT_E4_SIG);
    unsigned char* hook = (unsigned char*)(g_base + RVA_IDENT_E4_HOOK);

    if (memcmp(sig, IDENT_E4_SIG, sizeof(IDENT_E4_SIG)) != 0)
    {
        Log("IdentityFieldE4: сигнатура не совпала - не патчим");
        return false;
    }

    unsigned char* epi = (unsigned char*)(g_base + RVA_IDENT_E4_SKIP);
    if (memcmp(epi, IDENT_E4_EPI, sizeof(IDENT_E4_EPI)) != 0)
    {
        Log("IdentityFieldE4: эпилог rva %06X не совпал - не патчим", RVA_IDENT_E4_SKIP);
        return false;
    }

    g_identE4Resume = g_base + RVA_IDENT_E4_RESUME;
    g_identE4Skip = g_base + RVA_IDENT_E4_SKIP;

    unsigned char patch[6];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&IdentFieldE4Cave - ((DWORD)hook + 5);
    patch[5] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("IdentityFieldE4: установлен на rva %06X -> dll %08X",
        RVA_IDENT_E4_HOOK, (DWORD)(DWORD_PTR)&IdentFieldE4Cave);
    return true;
}

// Клиент 3.07, 1914-07-21: fieldE4 skip 40 раз, краш всё равно в
// FUN_005A63F0 (rva 0x5A63F7) — очистка «строки» по [esi+0xE4].
// Хук fieldE4 стоит только на cmp; путь `[esi+0xEC]==0` делает
// xorps и всё равно `lea edi,[esi+0xE4]; call 5A63F0`.
// Охрана всего FUN_005D1BB0 сразу после mov esi,ecx.
// 3.08: хук стоял на 1D1BCC (push esi) — сигнатура не совпала, пролог
// не встал, клиент дошёл до [vtable+0x70] в FUN_005D4420.
// Верно: 1D1BCD = mov esi,ecx; mov ecx,[esi+0x74]. Эпилог ждёт
// push edi — его ещё нет, поэтому перед jmp кладём.
static const DWORD RVA_IDENT_D1_HOOK   = 0x1D1BCD;
static const DWORD RVA_IDENT_D1_RESUME = 0x1D1BD2;  // xor ebx, ebx
static const unsigned char IDENT_D1_SIG[5] =
{
    0x8B, 0xF1,       // mov esi, ecx
    0x8B, 0x4E, 0x74  // mov ecx, [esi+0x74]
};

static DWORD g_identD1Resume = 0;
static DWORD g_identD1Skip = 0;

__declspec(naked) static void IdentFnD1Cave()
{
    __asm {
        mov esi, ecx
        push eax
        push 4
        push esi
        call IdentVtableCheck
        test eax, eax
        jz bad
        pop eax
        mov ecx, dword ptr [esi + 0x74]
        jmp dword ptr [g_identD1Resume]
    bad:
        pop eax
        push edi
        jmp dword ptr [g_identD1Skip]
    }
}

static bool InstallIdentityFnD1BB0()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_IDENT_D1_HOOK);
    if (memcmp(hook, IDENT_D1_SIG, sizeof(IDENT_D1_SIG)) != 0)
    {
        Log("IdentityFnD1BB0: сигнатура не совпала (%02X %02X %02X %02X %02X) - не патчим",
            hook[0], hook[1], hook[2], hook[3], hook[4]);
        return false;
    }

    unsigned char* epi = (unsigned char*)(g_base + RVA_IDENT_E4_SKIP);
    if (memcmp(epi, IDENT_E4_EPI, sizeof(IDENT_E4_EPI)) != 0)
    {
        Log("IdentityFnD1BB0: эпилог не совпал - не патчим");
        return false;
    }

    g_identD1Resume = g_base + RVA_IDENT_D1_RESUME;
    g_identD1Skip = g_base + RVA_IDENT_E4_SKIP;

    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&IdentFnD1Cave - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("IdentityFnD1BB0: установлен на rva %06X -> dll %08X",
        RVA_IDENT_D1_HOOK, (DWORD)(DWORD_PTR)&IdentFnD1Cave);
    return true;
}

// Клиент 3.08, 1914-07-23: пролог 5D1BB0 не встал, дошли до
// FUN_005D4420 rva 0x1D4445: mov edx,[esi]; mov edx,[edx+0x70].
// Два caller'а (1D20C5 из 1D1FC0 и 1D384E). this уже в esi.
// Хук после push ebp / mov ebp,esp, на mov eax,[esi+0xDC].
// Skip: leave; ret — push ebx/edi ещё не было.
static const DWORD RVA_IDENT_D4420_HOOK   = 0x1D4423;
static const DWORD RVA_IDENT_D4420_RESUME = 0x1D4429;  // sub esp, 0x18
static const unsigned char IDENT_D4420_SIG[6] =
{
    0x8B, 0x86, 0xDC, 0x00, 0x00, 0x00  // mov eax, [esi+0xDC]
};

static DWORD g_identD4420Resume = 0;

__declspec(naked) static void IdentFnD4420Cave()
{
    __asm {
        push 6
        push esi
        call IdentVtableCheck
        test eax, eax
        jz skip
        mov eax, dword ptr [esi + 0xDC]
        jmp dword ptr [g_identD4420Resume]
    skip:
        mov esp, ebp
        pop ebp
        ret
    }
}

static bool InstallIdentityFnD4420()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_IDENT_D4420_HOOK);
    if (memcmp(hook, IDENT_D4420_SIG, sizeof(IDENT_D4420_SIG)) != 0)
    {
        Log("IdentityFnD4420: сигнатура не совпала (%02X %02X %02X %02X %02X %02X) - не патчим",
            hook[0], hook[1], hook[2], hook[3], hook[4], hook[5]);
        return false;
    }

    g_identD4420Resume = g_base + RVA_IDENT_D4420_RESUME;

    unsigned char patch[6];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&IdentFnD4420Cave - ((DWORD)hook + 5);
    patch[5] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("IdentityFnD4420: установлен на rva %06X -> dll %08X",
        RVA_IDENT_D4420_HOOK, (DWORD)(DWORD_PTR)&IdentFnD4420Cave);
    return true;
}

static int __stdcall IdentPtrReadable(void* p, int n)
{
    if (!p || n <= 0)
        return 0;
    __try
    {
        volatile unsigned char sum = 0;
        unsigned char* b = (unsigned char*)p;
        sum = (unsigned char)(sum ^ b[0]);
        sum = (unsigned char)(sum ^ b[n - 1]);
        (void)sum;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// Клиент 3.09, 1914-08-18: пролог 5D1BB0 и 5D4420 стояли, lookup
// уже skip'нул тот же узел 4598FAD0 (1914-08-04). Через 14 дней
// FUN rva 0x113D50: eax=контейнер, ebx=[eax] identity,
// mov edx,[ebx]; mov eax,[edx+0x38] — vptr=B5BDF9FE вне exe.
// Четыре caller'а (719AC, 1E1D27, 2559C1, 426A10). Штатный выход
// пустого списка: pop edi; pop esi; mov al,1; pop ebx; leave; ret
// (rva 0x113D8B). На хуке edi ещё не push — без pop edi.
static const DWORD RVA_IDENT_113D_HOOK   = 0x113D5C;
static const DWORD RVA_IDENT_113D_RESUME = 0x113D63;  // push edi
static const unsigned char IDENT_113D_SIG[7] =
{
    0x8B, 0x18,             // mov ebx, [eax]
    0x8B, 0x13,             // mov edx, [ebx]
    0x8B, 0x42, 0x38        // mov eax, [edx+0x38]
};

static DWORD g_ident113DResume = 0;

__declspec(naked) static void IdentFn113DCave()
{
    __asm {
        test eax, eax
        jz skip
        push eax
        push 4
        push eax
        call IdentPtrReadable
        test eax, eax
        pop eax
        jz skip
        mov ebx, dword ptr [eax]
        push 7
        push ebx
        call IdentVtableCheck
        test eax, eax
        jz skip
        mov edx, dword ptr [ebx]
        mov eax, dword ptr [edx + 0x38]
        jmp dword ptr [g_ident113DResume]
    skip:
        pop esi
        mov al, 1
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

static bool InstallIdentityFn113D50()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_IDENT_113D_HOOK);
    if (memcmp(hook, IDENT_113D_SIG, sizeof(IDENT_113D_SIG)) != 0)
    {
        Log("IdentityFn113D50: сигнатура не совпала (%02X %02X %02X %02X %02X %02X %02X) - не патчим",
            hook[0], hook[1], hook[2], hook[3], hook[4], hook[5], hook[6]);
        return false;
    }

    g_ident113DResume = g_base + RVA_IDENT_113D_RESUME;

    unsigned char patch[7];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&IdentFn113DCave - ((DWORD)hook + 5);
    patch[5] = 0x90;
    patch[6] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("IdentityFn113D50: установлен на rva %06X -> dll %08X",
        RVA_IDENT_113D_HOOK, (DWORD)(DWORD_PTR)&IdentFn113DCave);
    return true;
}

// std::string/buffer dtor: mov eax,[edi]; mov esi,[eax+8]; free.
// Клиент 3.07: edi жив, [edi]=EDE8F0C3 не страница. Пустой путь —
// mov [edi],0 ... ret (rva 0x5A640A), без push esi.
static const DWORD RVA_STR_CLR_HOOK  = 0x5A63F0;
static const DWORD RVA_STR_CLR_CONT  = 0x5A63F6;  // push esi
static const DWORD RVA_STR_CLR_EMPTY = 0x5A640A;  // mov [edi],0
static const unsigned char STR_CLR_SIG[6] =
{
    0x8B, 0x07, 0x85, 0xC0, 0x74, 0x14
};

static DWORD g_strClrCont = 0;
static DWORD g_strClrEmpty = 0;

__declspec(naked) static void StrClearCave()
{
    __asm {
        push 12
        push edi
        call IdentPtrReadable
        test eax, eax
        jz ret_only
        mov eax, dword ptr [edi]
        test eax, eax
        jz empty
        push 12
        push eax
        call IdentPtrReadable
        test eax, eax
        jz badbuf
        mov eax, dword ptr [edi]
        jmp dword ptr [g_strClrCont]
    badbuf:
        push edi
        push 5
        call IdentNoteSkip
    empty:
        jmp dword ptr [g_strClrEmpty]
    ret_only:
        ret
    }
}

static bool InstallStringClearGuard()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_STR_CLR_HOOK);
    if (memcmp(hook, STR_CLR_SIG, sizeof(STR_CLR_SIG)) != 0)
    {
        Log("StringClearGuard: сигнатура не совпала - не патчим");
        return false;
    }

    g_strClrCont = g_base + RVA_STR_CLR_CONT;
    g_strClrEmpty = g_base + RVA_STR_CLR_EMPTY;

    unsigned char patch[6];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&StrClearCave - ((DWORD)hook + 5);
    patch[5] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("StringClearGuard: установлен на rva %06X -> dll %08X",
        RVA_STR_CLR_HOOK, (DWORD)(DWORD_PTR)&StrClearCave);
    return true;
}


// ---------------------------------------------------------------
// Переполнение буфера точек графика (0xc0000409 - сработала
// GS-канарейка стека - сразу за ним 0xc0000005 по тому же адресу).
//
// FUN_009e0ef0 - отрисовка графика истории (открывается вместе с
// окном бюджета). Для каждого сегмента истории код пишет ровно
// [ESI] точек подряд в фиксированный локальный буфер, без проверки
// [ESI] против вместимости буфера. Если у какого-то сегмента число
// точек аномально велико (экономические значения ломают счётчик
// записей истории - пользователь видел уходящие в минус числа
// пошлин перед крахом), запись уходит за пределы буфера и разносит
// стек.
//
// Патчим ровно точку первого чтения счётчика (rva 0x5E0FD6, 13
// байт - перекрывает cmp dword[esi],1; mov [esp+0x20],esi; jl
// rva 0x5E1159). Перед этим сравнением ограничиваем сам счётчик
// в записи (dword [esi]) сверху безопасным значением GRAPH_CLAMP_MAX
// - это временный UI-буфer графика, пересобираемый при каждом
// обновлении окна, поэтому обрезка не влияет на реальную
// экономическую статистику. Дальше воспроизводим оригинальные
// cmp/mov/jl без изменений - работают уже с обрезанным значением.
//
// ВАЖНО (версия 2.54 всё равно упала с этим же клампом=150):
// точный расчёт вместимости буфера по кадру стека функции -
// SUB ESP,0x9BC в прологе, буфер начинается с ESP+0x90 (первая
// запись пишет [ESI-4]..[ESI+0xB]), 3 push (EBX/ESI/EDI, 0xC байт)
// уже вычтены из ESP до этого места. По 0x10 байт на точку
// безопасный максимум = ((0x9BC-0xC) - 0x90) / 0x10 = 147 точек -
// значение 150 переполняет буфер ровно на столько, чтобы
// затереть сохранённый EBP/адрес возврата/параметры вызывающей
// функции (ровно это и дало крах в MOV ECX,[EAX] на rva 5E1137,
// где EAX = испорченный [EBP+0xC]). Взял 100 - заметный запас
// от математического предела 147 на случай неточности в ручном
// разборе кадра (выравнивание AND ESP,0xFFFFFFF8 даёт до 7 байт
// неопределённости).
// ---------------------------------------------------------------

static const DWORD RVA_GRAPH_CLAMP_HOOK   = 0x5E0FD6;
static const DWORD RVA_GRAPH_CLAMP_NORMAL = 0x5E0FE3;  // lea ecx,[esi+4]
static const DWORD RVA_GRAPH_CLAMP_SKIP   = 0x5E1159;  // dec dword ptr[esp+0x28] (следующий сегмент)
static const int   GRAPH_CLAMP_MAX = 100;

static const unsigned char GRAPH_CLAMP_SIG[13] =
{
    0x83, 0x3E, 0x01,                   // cmp dword ptr[esi],1
    0x89, 0x74, 0x24, 0x20,             // mov [esp+0x20],esi
    0x0F, 0x8C, 0x76, 0x01, 0x00, 0x00  // jl rva 0x5E1159
};

static bool InstallGraphPointClamp()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_GRAPH_CLAMP_HOOK);

    if (memcmp(hook, GRAPH_CLAMP_SIG, sizeof(GRAPH_CLAMP_SIG)) != 0)
    {
        Log("GraphPointClamp: сигнатура не совпала - не патчим");
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 48, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
        return false;

    int n = 0;

    cave[n++] = 0x81; cave[n++] = 0x3E;                     // cmp dword ptr[esi], GRAPH_CLAMP_MAX
    *(DWORD*)(cave + n) = (DWORD)GRAPH_CLAMP_MAX; n += 4;

    int jleAt = n;
    cave[n++] = 0x7E; cave[n++] = 0x00;                     // jle skip_clamp (адрес допишем ниже)

    cave[n++] = 0xC7; cave[n++] = 0x06;                     // mov dword ptr[esi], GRAPH_CLAMP_MAX
    *(DWORD*)(cave + n) = (DWORD)GRAPH_CLAMP_MAX; n += 4;

    int skipClampAt = n;
    cave[jleAt + 1] = (unsigned char)(skipClampAt - (jleAt + 2));

    // Воспроизводим перекрытые байты - теперь со значением,
    // ограниченным сверху GRAPH_CLAMP_MAX.
    cave[n++] = 0x83; cave[n++] = 0x3E; cave[n++] = 0x01;   // cmp dword ptr[esi],1
    cave[n++] = 0x89; cave[n++] = 0x74; cave[n++] = 0x24; cave[n++] = 0x20;  // mov [esp+0x20],esi

    int jgeAt = n;
    cave[n++] = 0x7D; cave[n++] = 0x00;                     // jge continue (адрес допишем ниже)

    cave[n++] = 0xE9;                                       // jmp far_skip (rva GRAPH_CLAMP_SKIP)
    *(DWORD*)(cave + n) = (g_base + RVA_GRAPH_CLAMP_SKIP) - (DWORD)(cave + n + 4);
    n += 4;

    int continueAt = n;
    cave[jgeAt + 1] = (unsigned char)(continueAt - (jgeAt + 2));

    cave[n++] = 0xE9;                                       // jmp far_resume (rva GRAPH_CLAMP_NORMAL)
    *(DWORD*)(cave + n) = (g_base + RVA_GRAPH_CLAMP_NORMAL) - (DWORD)(cave + n + 4);
    n += 4;

    unsigned char patch[13];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)cave - ((DWORD)hook + 5);
    for (int i = 5; i < 13; ++i)
        patch[i] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("GraphPointClamp: установлен на rva %06X, максимум %d точек, пещера %08X",
        RVA_GRAPH_CLAMP_HOOK, GRAPH_CLAMP_MAX, (DWORD)(DWORD_PTR)cave);
    return true;
}


// ---------------------------------------------------------------
// Диагностика краха экономики (временно, по запросу пользователя):
// живой дамп памяти state_building для regular_clothes_factory и
// canned_food_factory у России. У России именно эти два экземпляра
// фабрик уходят в аномальные money/pops_paychecks/last_income -
// цель дампа поймать момент/причину срыва по живым числам.
//
// Первая версия (2.53) вешала хук на FUN_004d04b0 ("можно ли
// построить этот тип здесь") - за время игры (сейв Proebali.v2,
// ~26 игровых дней от прошлого сейва) хук НИ РАЗУ не сработал.
// Разобрались почему: все 3 вызывающих места FUN_004d04b0 - это
// либо построение текста подсказки "почему нельзя строить"
// (FUN_0052ca30/FUN_0052cac0/FUN_00858670), либо ИИ, выбирающий,
// где построить новую фабрику (FUN_00857530/xref FUN_00858670) -
// а Россия в этом сейве под игроком (player="RUS"), так что ИИ её
// штаты вообще не оценивает, а нужные окна подсказок игрок не
// держал открытыми. Функция ежедневного пересчёта денег фабрики
// отдельная и не найдена (попытка через таблицу имён полей сейва
// FUN_00c381c0 второй раз подтвердила тупик - это просто регистрация
// имя<->индекс для сериализации, без офсетов/указателей на поля).
//
// Поэтому вместо хука - фоновый поток, который сам сканирует
// закоммиченную приватную (кучу) память процесса в поисках указателя
// на production_type с нужным именем (тот же struct-layout, что и
// раньше: node+0x18 = указатель на тип, имя лежит по OFF_PRODTYPE_NAME
// внутри объекта типа) - никакой зависимости от того, какая именно
// игровая функция и когда обращается к узлу. Найденные узлы дальше
// просто перечитываются раз в TRACK_INTERVAL_MS без нового скана.
// ---------------------------------------------------------------
static const char* const FACTORY_DUMP_NAMES[] = { "regular_clothes_factory", "canned_food_factory" };
static const int FACTORY_DUMP_NAME_COUNT = sizeof(FACTORY_DUMP_NAMES) / sizeof(FACTORY_DUMP_NAMES[0]);
static const int FACTORY_DUMP_MAX_NODES = 8;
static const int FACTORY_DUMP_RANGE = 0x300;
static const DWORD FACTORY_SCAN_INTERVAL_MS = 30000;  // полный скан памяти, пока не набрали узлов
static const DWORD FACTORY_TRACK_INTERVAL_MS = 5000;  // лёгкий перечит уже найденных узлов
static const DWORD FACTORY_RESCAN_INTERVAL_MS = 300000; // повторный полный скан на случай новых/пропавших фабрик

static void* g_factoryNodes[FACTORY_DUMP_MAX_NODES] = { 0 };
static char  g_factoryNodeNames[FACTORY_DUMP_MAX_NODES][64];
static int   g_factoryNodeCount = 0;
static HANDLE g_factoryScanThread = 0;

// Читает имя типа по typePtr+OFF_PRODTYPE_NAME с проверкой на
// печатность (иначе это почти наверняка не production_type, а
// случайное совпадение битов). Сам вызов защищён SEH снаружи.
static bool ReadPlausibleTypeName(void* typePtr, char* outName, int outSize)
{
    if (!typePtr)
        return false;
    // v2game.exe собран с LARGE_ADDRESS_AWARE - под WOW64 куча может
    // легитимно лежать выше 2 ГБ, поэтому верхняя граница почти у
    // самого потолка 32-битного адресного пространства, а не 0x7FFE0000.
    UINT_PTR tv = (UINT_PTR)typePtr;
    if (tv < 0x10000 || tv > 0xFFFE0000)
        return false;

    const char* src = (const char*)typePtr + OFF_PRODTYPE_NAME;
    int i = 0;
    for (; i < outSize - 1; ++i)
    {
        char c = src[i];
        if (c == 0)
            break;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e)
            return false;
        outName[i] = c;
    }
    if (i == 0 || i >= outSize - 1)
        return false;
    outName[i] = 0;
    return true;
}

static bool __cdecl SafeCheckTypeName(void* typePtr, char* outName, int outSize)
{
    __try
    {
        return ReadPlausibleTypeName(typePtr, outName, outSize);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static void DumpFactoryNodeNow(void* node, const char* typeName)
{
    char line[4096];
    int len = sprintf_s(line, sizeof(line), "FactoryDump node=%08X type=%s:",
        (unsigned)(DWORD_PTR)node, typeName);
    if (len < 0)
        return;

    __try
    {
        for (int off = 0; off < FACTORY_DUMP_RANGE; off += 8)
        {
            if (len >= (int)sizeof(line) - 64)
                break;
            double v = *(double*)((char*)node + off);
            // %e - см. комментарий в DumpAnomalyContext: %.3f на
            // экстремальном double разворачивается в сотни символов и
            // может увести sprintf_s в отказ (-1), а слепое накопление
            // len += -1 через несколько итераций уводит len в минус и
            // запись начинает бить перед началом buffer - это и поймал
            // /GS. Останавливаемся на первой же неудаче, не гадаем.
            int written = sprintf_s(line + len, sizeof(line) - len, " %03X=%.3e", off, v);
            if (written < 0)
                break;
            len += written;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (len >= 0 && len < (int)sizeof(line) - 32)
            sprintf_s(line + len, sizeof(line) - len, " <читать дальше нельзя>");
    }

    Log("%s", line);
}

// Диапазон "подозрительно больших" денежных значений: сломанные
// фабрики в сейве показывали money/pops_paychecks/last_income
// порядка -1.0e8..+1.2e8 - берём диапазон с запасом, но заведомо
// выше любых нормальных чисел экономики отдельной фабрики.
static const double FACTORY_ANOMALY_MIN = 1000000.0;
static const double FACTORY_ANOMALY_MAX = 1.0e9;
static const int FACTORY_ANOMALY_MAX_HITS = 25;

// Дамп окна ВОКРУГ найденного аномального числа (а не от начала
// узла, как DumpFactoryNodeNow - тут мы не знаем, с какого смещения
// начинается сам объект, поэтому смотрим и назад, и вперёд).
static void DumpAnomalyContext(void* addr)
{
    char line[4096];
    int len = sprintf_s(line, sizeof(line), "FactoryScan-anomaly addr=%08X:", (unsigned)(DWORD_PTR)addr);
    if (len < 0)
        return;

    __try
    {
        for (int off = -0x40; off < FACTORY_DUMP_RANGE; off += 8)
        {
            if (len >= (int)sizeof(line) - 64)
                break;
            double v = *(double*)((char*)addr + off);
            // %e вместо %f: ширина результата ограничена независимо от
            // величины числа. Окно вокруг addr - произвольные соседние
            // байты кучи, НЕ проверенная структура (в отличие от
            // DumpFactoryNodeNow) - среди них может попасться дикий
            // битовый паттерн около DBL_MAX, а %.3f на таком разворачивает
            // ~300-значную строку. Именно это один раз увело sprintf_s в
            // отказ (-1), после чего len += -1 несколько раз подряд ушёл
            // в минус и запись сама начала бить перед началом buffer -
            // ровно то, что поймал /GS. Сейчас же ещё и не суммируем
            // отрицательный результат вслепую - на первой же неудаче
            // просто останавливаемся.
            int written = sprintf_s(line + len, sizeof(line) - len, " %04X=%.3e", off & 0xFFFF, v);
            if (written < 0)
                break;
            len += written;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (len >= 0 && len < (int)sizeof(line) - 32)
            sprintf_s(line + len, sizeof(line) - len, " <дальше нельзя>");
    }

    Log("%s", line);
}

static void RegisterFoundNode(void* node, const char* typeName)
{
    for (int i = 0; i < g_factoryNodeCount; ++i)
        if (g_factoryNodes[i] == node)
            return;
    if (g_factoryNodeCount >= FACTORY_DUMP_MAX_NODES)
        return;

    int slot = g_factoryNodeCount++;
    g_factoryNodes[slot] = node;
    strcpy_s(g_factoryNodeNames[slot], typeName);
    Log("FactoryScan: найден узел node=%08X type=%s (всего найдено %d)",
        (unsigned)(DWORD_PTR)node, typeName, g_factoryNodeCount);
    DumpFactoryNodeNow(node, typeName);
}

// Индекс читаемых регионов (любой committed+readable, не только
// MEM_PRIVATE - в отличие от основного скана ниже, сюда попадает и
// .data/.rdata и т.п., т.к. кандидат-указатель на production_type
// в принципе может указывать куда угодно). Нужен, чтобы НЕ ловить
// исключение на каждом мусорном "похожем на указатель" 4-байтовом
// значении - сама обработка access violation на порядок дороже
// самого сравнения диапазонов. VirtualQuery отдаёт регионы строго
// по возрастанию адреса, поэтому можно сразу бинарным поиском.
static const int FACTORY_SCAN_MAX_RANGES = 8192;
struct AddrRange { UINT_PTR start, end; };
static AddrRange g_readableRanges[FACTORY_SCAN_MAX_RANGES];
static int g_readableRangeCount = 0;

static void BuildReadableRangeIndex()
{
    g_readableRangeCount = 0;
    MEMORY_BASIC_INFORMATION mbi;
    BYTE* addr = 0;

    while (VirtualQuery(addr, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        BYTE* regionEnd = (BYTE*)mbi.BaseAddress + mbi.RegionSize;

        bool readable = (mbi.State == MEM_COMMIT)
            && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0
            && (mbi.Protect & PAGE_GUARD) == 0;

        if (readable && g_readableRangeCount < FACTORY_SCAN_MAX_RANGES)
        {
            g_readableRanges[g_readableRangeCount].start = (UINT_PTR)mbi.BaseAddress;
            g_readableRanges[g_readableRangeCount].end = (UINT_PTR)regionEnd;
            g_readableRangeCount++;
        }

        if (regionEnd <= addr)
            break;
        addr = regionEnd;
    }
}

// Бинарный поиск: лежит ли [start, start+len) целиком в одном уже
// проверенном читаемом регионе.
static bool IsRangeReadable(UINT_PTR start, UINT_PTR len)
{
    UINT_PTR endAddr = start + len;
    int lo = 0, hi = g_readableRangeCount - 1, found = -1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        if (g_readableRanges[mid].start <= start) { found = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (found < 0)
        return false;
    return g_readableRanges[found].end >= endAddr;
}

// Полный скан приватной read/write памяти процесса. Каждый регион
// защищён одним SEH-блоком (страница могла исчезнуть за время скана
// из-за параллельной работы игры) - это дешевле, чем оборачивать
// каждое 4-байтовое чтение отдельно, а сами чтения внутри региона
// не выходят за его проверенные границы. Кандидат-указатель сначала
// проверяется по индексу читаемых регионов (IsRangeReadable) - это
// быстрое сравнение чисел без обращения к памяти - и только если он
// проходит, читаем его (ещё раз под SEH - индекс мог устареть за
// время скана, но это уже редкий случай, а не почти каждый кандидат).
static void FullMemoryScan()
{
    DWORD startTick = GetTickCount();
    BuildReadableRangeIndex();
    Log("FactoryScan: индекс читаемых регионов построен (%d%s), начинаю скан...",
        g_readableRangeCount,
        g_readableRangeCount >= FACTORY_SCAN_MAX_RANGES ? " - ДОСТИГНУТ ЛИМИТ, часть регионов пропущена" : "");

    MEMORY_BASIC_INFORMATION mbi;
    BYTE* addr = 0;

    // Диагностика (временно): считаем совпадения по ВСЕМ известным
    // именам типов производства (g_productionTypeNames, все 65 - не
    // только наши 2 целевых), чтобы понять, работает ли сам механизм
    // поиска в принципе, или дело именно в этих двух фабриках.
    //
    // ВАЖНО: регионы для СКАНИРОВАНИЯ (не индекс читаемости - тот
    // по-прежнему смотрит всё) снова ограничены MEM_PRIVATE. В 2.58
    // это ограничение снималось "на всякий случай", и практически
    // сразу после этого пользователь получил "Failed to create a
    // graphics device" при запуске игры - с MEM_PRIVATE-only (версии
    // 2.55-2.57) такого не было ни разу за несколько прогонов. Точный
    // механизм не доказан (вероятно, что-то вроде guard-страниц/
    // внутренней сигнализации видеодрайвера в его MEM_MAPPED/MEM_IMAGE
    // данных, потревоженное чтением из чужого потока), но раз риск
    // подтверждён эмпirически - за пределы кучи процесса не выходим.
    int diagAnyMatches = 0;
    char diagExamples[5][64];
    int diagExampleCount = 0;
    int anomalyHits = 0;

    while (VirtualQuery(addr, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        BYTE* regionEnd = (BYTE*)mbi.BaseAddress + mbi.RegionSize;

        bool scannable = (mbi.State == MEM_COMMIT)
            && (mbi.Type == MEM_PRIVATE)
            && (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) != 0
            && (mbi.Protect & PAGE_GUARD) == 0;

        if (scannable)
        {
            __try
            {
                BYTE* p = (BYTE*)mbi.BaseAddress;
                BYTE* end = regionEnd - sizeof(void*);
                for (; p < end; p += 4)
                {
                    if (anomalyHits < FACTORY_ANOMALY_MAX_HITS && p + 8 <= regionEnd)
                    {
                        double v = *(double*)p;
                        double av = v < 0 ? -v : v;
                        if (av >= FACTORY_ANOMALY_MIN && av <= FACTORY_ANOMALY_MAX)
                        {
                            anomalyHits++;
                            DumpAnomalyContext(p);
                        }
                    }

                    void* candidate = *(void**)p;
                    UINT_PTR cv = (UINT_PTR)candidate;
                    if (cv < 0x10000 || cv > 0xFFFE0000)
                        continue;
                    if (!IsRangeReadable(cv + OFF_PRODTYPE_NAME, 64))
                        continue;

                    char name[64];
                    if (!SafeCheckTypeName(candidate, name, sizeof(name)))
                        continue;

                    bool anyTypeMatch = false;
                    for (int t = 0; t < g_productionTypeCount; ++t)
                        if (strcmp(g_productionTypeNames[t], name) == 0) { anyTypeMatch = true; break; }
                    if (anyTypeMatch)
                    {
                        diagAnyMatches++;
                        if (diagExampleCount < 5)
                        {
                            strcpy_s(diagExamples[diagExampleCount], name);
                            diagExampleCount++;
                        }
                    }

                    bool match = false;
                    for (int i = 0; i < FACTORY_DUMP_NAME_COUNT; ++i)
                        if (strcmp(FACTORY_DUMP_NAMES[i], name) == 0) { match = true; break; }
                    if (!match)
                        continue;

                    void* node = p - 0x18;
                    RegisterFoundNode(node, name);
                    if (g_factoryNodeCount >= FACTORY_DUMP_MAX_NODES)
                        break;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // регион пропал/перезащитился посреди скана - пропускаем его
            }
        }

        if (g_factoryNodeCount >= FACTORY_DUMP_MAX_NODES)
            break;
        if (regionEnd <= addr)
            break;
        addr = regionEnd;
    }

    char examplesLine[400];
    int el = 0;
    examplesLine[0] = 0;
    for (int i = 0; i < diagExampleCount; ++i)
    {
        int written = sprintf_s(examplesLine + el, sizeof(examplesLine) - el, "%s%s", i ? ", " : "", diagExamples[i]);
        if (written < 0)
            break;
        el += written;
    }

    Log("FactoryScan: полный скан завершён за %u мс, узлов всего %d; совпадений по ЛЮБОМУ известному типу производства: %d (примеры: %s); аномальных чисел (%.0f..%.0f): %d",
        GetTickCount() - startTick, g_factoryNodeCount, diagAnyMatches, examplesLine,
        FACTORY_ANOMALY_MIN, FACTORY_ANOMALY_MAX, anomalyHits);
}

static DWORD WINAPI FactoryScanThreadProc(LPVOID)
{
    Sleep(15000); // дать игре загрузиться перед первым сканом

    DWORD lastFullScan = 0;
    for (;;)
    {
        DWORD now = GetTickCount();
        bool needFullScan = (g_factoryNodeCount < FACTORY_DUMP_MAX_NODES)
            ? (now - lastFullScan >= FACTORY_SCAN_INTERVAL_MS)
            : (now - lastFullScan >= FACTORY_RESCAN_INTERVAL_MS);

        if (needFullScan || lastFullScan == 0)
        {
            FullMemoryScan();
            lastFullScan = GetTickCount();
        }
        else
        {
            for (int i = 0; i < g_factoryNodeCount; ++i)
            {
                char name[64];
                if (!SafeCheckTypeName(*(void**)((char*)g_factoryNodes[i] + 0x18), name, sizeof(name)))
                {
                    Log("FactoryScan: узел node=%08X (%s) больше не читается - похоже, снесён",
                        (unsigned)(DWORD_PTR)g_factoryNodes[i], g_factoryNodeNames[i]);
                    continue;
                }
                DumpFactoryNodeNow(g_factoryNodes[i], g_factoryNodeNames[i]);
            }
        }

        Sleep(FACTORY_TRACK_INTERVAL_MS);
    }
    return 0;
}

static bool InstallFactoryDumpScan()
{
    g_factoryScanThread = CreateThread(0, 0, FactoryScanThreadProc, 0, 0, 0);
    if (!g_factoryScanThread)
    {
        Log("FactoryScan: не удалось создать поток");
        return false;
    }
    Log("FactoryScan: поток запущен");
    return true;
}


// ---------------------------------------------------------------
// Установка
// ---------------------------------------------------------------

// Таблицы лежат в .rdata, поэтому запись только со снятием защиты —
// иначе она молча не проходит.
static bool PatchSlot(DWORD rvaVtable, int slotIndex, void* replacement, void** outOriginal)
{
    void** slot = (void**)(g_base + rvaVtable) + slotIndex;

    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    if (outOriginal)
        *outOriginal = *slot;

    *slot = replacement;

    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    return true;
}



// ---------------------------------------------------------------
// Общее население в индикаторе верхней панели
//
// Игра хранит в стране по +0x12E8 только взрослое мужское население
// и показывает его в панели как есть. Общее получается умножением
// на 4 — так же делает подсказка TOPBAR_POPULATION_VISUAL, где
// умножение зашито в код инструкцией lea eax,[ebx*4].
//
// Умножаем на выводе, а не у источника: само поле участвует в
// расчётах налогов, призыва и влияния, и трогать его нельзя.
//
// Подсказка "Наше взрослое мужское население сейчас" намеренно не
// правится: там нужно исходное число.
// ---------------------------------------------------------------

static const int POP_MULTIPLIER_SHIFT = 2;   // сдвиг на 2 = умножение на 4

// Места отрисовки. Каждое — чтение поля населения в регистр, сразу
// за которым идёт форматирование числа. Шесть байт чтения меняем на
// переход в пещеру: там читаем, сдвигаем и возвращаемся.
//
// Добавить новое место: найти в Ghidra чтение [reg+0x12E8] рядом с
// подстановкой в текст, вписать RVA и байты. Реестр регистров в
// сигнатуре: 8B 87 = EAX,[EDI]; 8B 81 = EAX,[ECX]; 8B 83 = EAX,[EBX].
//
// Важно: править только отрисовку. Само поле участвует в расчётах
// налогов, призыва и влияния, и трогать его нельзя.
struct PopSite
{
    const char* name;
    DWORD         rva;
    unsigned char sig[6];
    bool          enabled;
};

static PopSite POP_SITES[] =
{
    { "topbar",    0x310A32, { 0x8B, 0x87, 0xE8, 0x12, 0x00, 0x00 }, true },
    { "diplomacy", 0x22880F, { 0x8B, 0x81, 0xE8, 0x12, 0x00, 0x00 }, true },
    { "lobby",     0x36DFBB, { 0x8B, 0x80, 0xE8, 0x12, 0x00, 0x00 }, true },
};

static const int POP_SITE_COUNT = sizeof(POP_SITES) / sizeof(POP_SITES[0]);


static bool InstallPopSite(const PopSite& site)
{
    unsigned char* hook = (unsigned char*)(g_base + site.rva);

    if (memcmp(hook, site.sig, 6) != 0)
    {
        Log("PopDisplay '%s': сигнатура не совпала - не патчим", site.name);
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
        return false;

    int n = 0;

    // Оригинальное чтение поля — как есть, вместе с регистром.
    memcpy(cave + n, site.sig, 6);
    n += 6;

    // Умножение. Приёмник у всех сигнатур EAX, поэтому сдвигаем его.
    // Флаги никому не нужны: следом идёт push.
    cave[n++] = 0xC1; cave[n++] = 0xE0;
    cave[n++] = (unsigned char)POP_MULTIPLIER_SHIFT;   // shl eax, N

    cave[n++] = 0xE9;                                  // jmp обратно
    *(DWORD*)(cave + n) = (g_base + site.rva + 6) - (DWORD)(cave + n + 4);
    n += 4;

    // Шесть перекрываемых байт: пять под jmp и один nop.
    unsigned char patch[6];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)cave - ((DWORD)hook + 5);
    patch[5] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("PopDisplay '%s': rva %06X, пещера %08X",
        site.name, site.rva, (DWORD)(DWORD_PTR)cave);
    return true;
}


static void InstallPopDisplay()
{
    Log("PopDisplay: множитель %d", 1 << POP_MULTIPLIER_SHIFT);

    for (int i = 0; i < POP_SITE_COUNT; ++i)
    {
        if (POP_SITES[i].enabled)
            InstallPopSite(POP_SITES[i]);
    }
}




// ---------------------------------------------------------------
// Версия мода в подписи главного меню
//
// Проверено в Cheat Engine на живой памяти (декомпиляция путала
// адреса — между двумя push оказалась ещё mov edi,0xF, а адрес
// строки не совпадал со значением из декомпилятора):
//
//   233826  6A 08                  push 0x8
//   233828  BF 0F 00 00 00         mov edi, 0xF        (не трогаем)
//   23382D  68 4C 76 A0 4C         push адрес "V2 v3.04"
//   233832  8D 4D B0               lea ecx,[ebp-0x50]
//
// Перекрываем все 12 байт (push + mov + push) переходом в пещеру,
// где пушим длину и адрес своей строки, повторяем mov edi,0xF как
// есть — на случай, если он используется дальше по функции — и
// возвращаемся на lea ecx по 233832.
// ---------------------------------------------------------------

static const DWORD RVA_VERLABEL_HOOK = 0x233826;
static const DWORD RVA_VERLABEL_RESUME = 0x233832;

// Первые 8 байт постоянны, дальше идёт push абсолютного адреса —
// его нельзя зашивать константой: это база+RVA, а база меняется
// от ASLR при каждом запуске. Проверяем отдельно, относительно
// текущего g_base.
static const unsigned char VERLABEL_SIG[8] =
{
    0x6A, 0x08,                          // push 0x8
    0xBF, 0x0F, 0x00, 0x00, 0x00,        // mov edi, 0xF
    0x68                                 // push imm32 (адрес — далее)
};

static const DWORD RVA_VERLABEL_ORIG_STR = 0xA0764C;

static char g_versionLabel[128];

static bool InstallVersionLabel()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_VERLABEL_HOOK);

    if (memcmp(hook, VERLABEL_SIG, sizeof(VERLABEL_SIG)) != 0)
    {
        Log("VersionLabel: сигнатура не совпала (%02X %02X %02X %02X %02X %02X %02X %02X) - не патчим",
            hook[0], hook[1], hook[2], hook[3], hook[4], hook[5], hook[6], hook[7]);
        return false;
    }

    DWORD origAddr = *(DWORD*)(hook + 8);
    if (origAddr != g_base + RVA_VERLABEL_ORIG_STR)
    {
        Log("VersionLabel: адрес строки не совпал (%08X, ожидали %08X) - не патчим",
            origAddr, g_base + RVA_VERLABEL_ORIG_STR);
        return false;
    }

    sprintf_s(g_versionLabel, sizeof(g_versionLabel),
        "V2 v3.04 + V2DLL v%s", MOD_VERSION);

    unsigned len = (unsigned)strlen(g_versionLabel);

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
        return false;

    int n = 0;

    cave[n++] = 0x68;                                  // push imm32 (длина)
    *(DWORD*)(cave + n) = len; n += 4;

    cave[n++] = 0x68;                                  // push imm32 (адрес)
    *(DWORD*)(cave + n) = (DWORD)(DWORD_PTR)g_versionLabel; n += 4;

    // mov edi, 0xF — повторяем как есть, вдруг используется дальше.
    cave[n++] = 0xBF;
    *(DWORD*)(cave + n) = 0xF; n += 4;

    cave[n++] = 0xE9;                                  // jmp обратно
    *(DWORD*)(cave + n) = (g_base + RVA_VERLABEL_RESUME) - (DWORD)(cave + n + 4);
    n += 4;

    unsigned char patch[12];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)cave - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("VersionLabel: '%s' (len=%u), пещера %08X",
        g_versionLabel, len, (DWORD)(DWORD_PTR)cave);
    return true;
}


// ---------------------------------------------------------------
// Разброс броска в бою (COMBAT_ROLL_MIN..COMBAT_ROLL_MAX).
//
// Формула броска раньше была константой прямо в точке вызова:
// FUN_0059ca40 (разрешение раунда боя) 4 раза вызывает генератор
// случайных чисел и берёт остаток от деления на 10. Сторонний
// Vic2_Roll_Changer.py (скрипт лежит в V2BDSM, не часть нашего DLL)
// статически переписал exe: все 4 места вызова теперь зовут общую
// подпрограмму в неиспользуемом хвосте секции .text (RVA 0x889113),
// которая делает mov ecx,<модуль>; idiv ecx; add edx,<минимум>; ret -
// подтверждено в Ghidra (get_xrefs_to на адрес пещеры даёт ровно эти
// 4 вызова из FUN_0059ca40, а её result сохраняется в +0x30 у каждой
// из двух сторон боя). Этот exe уже был пропатчен так на диапазон
// 2-5 - мы просто переписываем модуль/минимум в той же пещере на
// лету при каждом запуске, без изменения файла.
//
// Если exe НИКОГДА не патчился этим скриптом (пещеры нет, все 4
// вызова всё ещё делают "cdq; mov ecx,0Ah; idiv ecx" инлайном) -
// сигнатура не совпадёт, и мы это НЕ чиним: создание новой пещеры и
// переброс 4 вызовов - отдельная задача, которую можно сделать тем
// же скриптом или отдельным патчем позже.
// ---------------------------------------------------------------

static const DWORD RVA_COMBAT_ROLL_CAVE = 0x889113;

// Байты 0-1 пещеры (изначально "F2 00") в сигнатуру не входят: это
// хвост абсолютного адреса из ВАНИЛЬНОГО кода на этом месте (сам
// скрипт-патчер его не трогал, начал писать только с байта 10 - B9),
// и он остался под релокационной записью exe. Загрузчик Windows сам
// правит эти 2 байта под дельту ASLR при каждом запуске (проверено
// живьём: база 00FD0000 вместо предпочитаемой 00400000, дельта
// 00BD0000 - и байты 0-1 стали AF 01 вместо F2 00, ровно на старшее
// слово дельты). Байты 11..14 (модуль, mov ecx,imm32) и байт 19
// (минимум, add edx,imm8) - тоже не в сигнатуре, но по другой причине:
// это переменные данные, которые мы сами переписываем.
static const unsigned char COMBAT_ROLL_PREFIX[9] =
{ 0xE9, 0x96, 0x5C, 0xE9, 0xFF, 0x00, 0x00, 0x00, 0xB9 };
static const unsigned char COMBAT_ROLL_MID[4] = { 0xF7, 0xF9, 0x83, 0xC2 };

static bool InstallCombatRoll()
{
    if (g_settings.combatRollMin < 0 || g_settings.combatRollMin > 127 ||
        g_settings.combatRollMax < g_settings.combatRollMin)
    {
        Log("CombatRoll: некорректный диапазон %d..%d - не патчим",
            g_settings.combatRollMin, g_settings.combatRollMax);
        return false;
    }

    unsigned char* cave = (unsigned char*)(g_base + RVA_COMBAT_ROLL_CAVE);

    if (memcmp(cave + 2, COMBAT_ROLL_PREFIX, sizeof(COMBAT_ROLL_PREFIX)) != 0 ||
        memcmp(cave + 15, COMBAT_ROLL_MID, sizeof(COMBAT_ROLL_MID)) != 0 ||
        cave[20] != 0xC3)
    {
        Log("CombatRoll: пещера не найдена (exe не пропатчен Vic2_Roll_Changer.py?) - не патчим. "
            "base=%08X rva=%08X байты: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
            "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            g_base, RVA_COMBAT_ROLL_CAVE,
            cave[0], cave[1], cave[2], cave[3], cave[4], cave[5], cave[6], cave[7], cave[8], cave[9], cave[10],
            cave[11], cave[12], cave[13], cave[14], cave[15], cave[16], cave[17], cave[18], cave[19], cave[20]);
        return false;
    }

    DWORD modulo = (DWORD)(g_settings.combatRollMax - g_settings.combatRollMin + 1);

    DWORD oldProtect = 0;
    if (!VirtualProtect(cave, 21, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    *(DWORD*)(cave + 11) = modulo;
    cave[19] = (unsigned char)g_settings.combatRollMin;

    VirtualProtect(cave, 21, oldProtect, &oldProtect);

    Log("CombatRoll: диапазон %d..%d (модуль=%u)",
        g_settings.combatRollMin, g_settings.combatRollMax, modulo);
    return true;
}


// ---------------------------------------------------------------
// Диагностика бага с чек-суммой (временный патч, не для релиза)
//
// Баг: чек-сумма в углу экрана меняется на одну букву после входа в
// партию (одиночную или сетевую) - воспроизводится и на ванили, без
// нашего мода. FUN_006377a0 (RVA 0x2377a0) считает "файловую"
// чек-сумму; get_xrefs_to в Ghidra нашёл только один статический
// вызов (из инициализации приложения, до главного меню) - но это не
// исключает, что счётчик читается позже ещё раз кодом внутри этой же
// функции при повторном входе, либо вызов идёт откуда-то косвенно.
//
// Первая попытка (хук на FUN_0076b4b0, копирование чек-суммы в
// структуру лобби при входе в партию) дала raw=0 каждый раз - то
// место читает поле ДО того, как оно реально заполнено, тупик.
//
// Вторая попытка (эта): реальный аккумулятор чек-суммы - раскрыт в
// дизасме, прямо перед тем, как строится "Checksum is <value>":
//   00638a4b: MOV EDX,[ECX+0x30]   ; ECX = param_1 (this), +0x30 -
//                                    итоговое целое чек-суммы
//   00638a4e: PUSH 0xe07d2c        ; "Checksum is "
// Перехватываем блок из 3 инструкций перед этим чтением (11 байт,
// rva 0x2384a0..0x2384ab: PUSH EBX; PUSH 0xC; MOV byte[ESP+0x3D4],0x30),
// логируем ECX и *(ECX+0x30), затем воспроизводим эти 3 инструкции и
// возвращаемся - "MOV EDX,[ECX+0x30]" после нас выполняется как есть,
// нетронутой.
// ---------------------------------------------------------------

static const DWORD RVA_CHECKSUM_HOOK   = 0x238A40;
static const DWORD RVA_CHECKSUM_RESUME = 0x238A4B;

static const unsigned char CHECKSUM_HOOK_SIG[11] =
{
    0x53,                                     // push ebx
    0x6A, 0x0C,                               // push 0xC
    0xC6, 0x84, 0x24, 0xD4, 0x03, 0x00, 0x00, 0x30  // mov byte ptr [esp+0x3D4],0x30
};

static DWORD g_checksumResumeAddr = 0;
static int g_checksumHookHits = 0;

// Сохраняем указатель "this" из ChecksumCompute, чтобы позже (при
// входе в лобби) перечитать ТОТ ЖЕ +0x30 напрямую, без вызова функции
// целиком - проверяем, не правится ли аккумулятор тихо, в обход
// FUN_006377a0.
static void* g_checksumAppPtr = 0;

static void __cdecl LogChecksumCompute(void* param1)
{
    ++g_checksumHookHits;
    g_checksumAppPtr = param1;

    int checksum = 0;
    __try
    {
        checksum = *(int*)((char*)param1 + 0x30);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("ChecksumCompute[%d]: param1=%08X - память +0x30 не читается",
            g_checksumHookHits, (unsigned)(DWORD_PTR)param1);
        return;
    }

    Log("ChecksumCompute[%d]: param1=%08X value=%d (%08X)",
        g_checksumHookHits, (unsigned)(DWORD_PTR)param1, checksum, (unsigned)checksum);
}

__declspec(naked) static void ChecksumComputeThunk()
{
    __asm {
        push ecx
        push edx
        push eax
        push ecx
        call LogChecksumCompute
        add esp, 4
        pop eax
        pop edx
        pop ecx
        push ebx
        push 0x0C
        mov byte ptr [esp + 0x3D4], 0x30
        jmp dword ptr [g_checksumResumeAddr]
    }
}

static bool InstallChecksumDiagnostic()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_CHECKSUM_HOOK);

    if (memcmp(hook, CHECKSUM_HOOK_SIG, sizeof(CHECKSUM_HOOK_SIG)) != 0)
    {
        Log("ChecksumCompute: сигнатура не совпала (%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X) - не патчим",
            hook[0], hook[1], hook[2], hook[3], hook[4], hook[5], hook[6], hook[7], hook[8], hook[9], hook[10]);
        return false;
    }

    g_checksumResumeAddr = g_base + RVA_CHECKSUM_RESUME;

    unsigned char patch[11];
    memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&ChecksumComputeThunk - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("ChecksumCompute: установлен на rva %06X", RVA_CHECKSUM_HOOK);
    return true;
}

// Третий хук: FUN_0076b4b0 (RVA 0x36b4b0) - подтверждено, срабатывает
// при каждом входе в лобби/партию (3 раза на 3 входа в прошлом тесте).
// На этот раз не просто читаем её собственный [EAX+0x130] (там всегда
// 0 - уже проверено), а ЗАОДНО перечитываем аккумулятор чек-суммы из
// ChecksumCompute напрямую по сохранённому g_checksumAppPtr - если он
// меняется между входами, значит его правит что-то в обход
// FUN_006377a0 (которая, как подтвердил ChecksumCompute, вызывается
// только один раз за сессию).
static const DWORD RVA_LOBBY_ENTRY_HOOK   = 0x36B4F6;
static const DWORD RVA_LOBBY_ENTRY_RESUME = 0x36B4FC;

static const unsigned char LOBBY_ENTRY_SIG[6] = { 0x8B, 0x80, 0x30, 0x01, 0x00, 0x00 };

static DWORD g_lobbyEntryResumeAddr = 0;
static int g_lobbyEntryHits = 0;

static void __cdecl LogLobbyEntry(void* pObj)
{
    ++g_lobbyEntryHits;

    DWORD raw = 0;
    __try { raw = *(DWORD*)((char*)pObj + 0x130); }
    __except (EXCEPTION_EXECUTE_HANDLER) { raw = 0; }

    if (g_checksumAppPtr == 0)
    {
        Log("LobbyEntry[%d]: pObj=%08X raw130=%08X (g_checksumAppPtr ещё не установлен)",
            g_lobbyEntryHits, (unsigned)(DWORD_PTR)pObj, raw);
        return;
    }

    int accum = 0;
    __try { accum = *(int*)((char*)g_checksumAppPtr + 0x30); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("LobbyEntry[%d]: pObj=%08X raw130=%08X, аккумулятор (%08X+0x30) не читается",
            g_lobbyEntryHits, (unsigned)(DWORD_PTR)pObj, raw, (unsigned)(DWORD_PTR)g_checksumAppPtr);
        return;
    }

    Log("LobbyEntry[%d]: pObj=%08X raw130=%08X, аккумулятор сейчас=%d (%08X)",
        g_lobbyEntryHits, (unsigned)(DWORD_PTR)pObj, raw, accum, (unsigned)accum);
}

__declspec(naked) static void LobbyEntryThunk()
{
    __asm {
        push eax
        push ecx
        push edx
        push eax
        call LogLobbyEntry
        add esp, 4
        pop edx
        pop ecx
        pop eax
        mov eax, [eax + 0x130]
        jmp dword ptr [g_lobbyEntryResumeAddr]
    }
}

static bool InstallLobbyEntryHook()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_LOBBY_ENTRY_HOOK);

    if (memcmp(hook, LOBBY_ENTRY_SIG, sizeof(LOBBY_ENTRY_SIG)) != 0)
    {
        Log("LobbyEntry: сигнатура не совпала (%02X %02X %02X %02X %02X %02X) - не патчим",
            hook[0], hook[1], hook[2], hook[3], hook[4], hook[5]);
        return false;
    }

    g_lobbyEntryResumeAddr = g_base + RVA_LOBBY_ENTRY_RESUME;

    unsigned char patch[6];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&LobbyEntryThunk - ((DWORD)hook + 5);
    patch[5] = 0x90;

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("LobbyEntry: установлен на rva %06X", RVA_LOBBY_ENTRY_HOOK);
    return true;
}

// ---------------------------------------------------------------
// OOS: FUN_00682EC0 (RVA 0x282EC0) — единственный билдер диалога
// "Games out of synch" / OOS_TITLE. Один caller (0072ECF0):
//   mov eax, [g_session]; add ecx, 0x3C; push ecx; push eax;
//   call FUN_00682EC0; ret
// После DIFF нет паузы и нет дисконнекта — caller сразу ret.
// В прологе функции: mov byte [session+0xB20], 1 (каждый день,
// до сравнения). Дальше call сверки векторов; jz ~+0x8D8
// пропускает ~2 КБ билдера диалога при MATCH. Игра тикает дальше.
// Пишем Logs\v2dll_oos.log на каждый дневной вызов (SYNC и DIFF).
// Пролог 55 8B EC 6A FF + уникальный sub esp,0x140.
//
// Игра сверяет std::vector<dword> локальной сессии (arg0+0xB74) с
// вектором пира (arg1 = packet+0x3C, команда "ingame_session").
// FUN_00682EC0 вызывается КАЖДЫЙ игровой день в MP, не только при
// OOS: при совпадении диалог не строится. Слотов обычно два
// (resize(2)): слот 0 — аддитивная сумма/счётчик (растёт ~тысячи
// за день), слот 1 в этом билде всегда 0. Дата в +0xB0C — часы,
// эпоха 0x29C55C0 = 5000*365*24.
// Рядом session+0xB84 — вектор 16-байтных записей (ptr + 3 dword).
// В пакет MP он не входит; при OOS пишем локальный дамп, чтобы
// два клиента сверили его между собой.
// ---------------------------------------------------------------

static const DWORD RVA_OOS_REPORT = 0x282EC0;
static const unsigned char OOS_POST_SEH[11] =
    { 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00, 0x53, 0x56, 0x8B, 0x75, 0x08 };

static DWORD g_oosResume = 0;
static int g_oosHits = 0;
static int g_syncHits = 0;
static char g_lastChecksumLine[256] = "none";

static void RememberChecksum(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(g_lastChecksumLine, sizeof(g_lastChecksumLine), _TRUNCATE, fmt, ap);
    va_end(ap);
}

static void LogOosFile(const char* fmt, ...)
{
    InitLogDir();

    if (g_logCsInit)
        EnterCriticalSection(&g_logCs);

    FILE* f = 0;
    if (_wfopen_s(&f, g_oosLogFile, g_oosLogStarted ? L"a" : L"w") != 0 || !f)
    {
        if (g_logCsInit)
            LeaveCriticalSection(&g_logCs);
        return;
    }
    g_oosLogStarted = true;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
        (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
        (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
        (unsigned)st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);

    if (g_logCsInit)
        LeaveCriticalSection(&g_logCs);
}

static void DumpPtrLine(const char* tag, void* p)
{
    unsigned d[8];
    memset(d, 0, sizeof(d));
    int ok = 0;
    __try
    {
        memcpy(d, p, sizeof(d));
        ok = 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = 0;
    }

    if (!ok)
    {
        LogOosFile("  %s=%08X unreadable", tag, (unsigned)(DWORD_PTR)p);
        return;
    }
    LogOosFile("  %s=%08X %08X %08X %08X %08X %08X %08X %08X %08X",
        tag, (unsigned)(DWORD_PTR)p,
        d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
}

static const int OOS_VEC_OFF = 0xB74;
static const int OOS_DATE_OFF = 0xB0C;
static const int OOS_FLAG_OFF = 0xB20;
static const int OOS_REC_OFF = 0xB84;
static const int OOS_MAX_SLOTS = 256;
static const int OOS_REC_MAX = 256;
static const int OOS_DATE_EPOCH = 0x029C55C0;

static const char* OosSlotLabel(int i)
{
    if (i == 0)
        return "sum";
    if (i == 1)
        return "aux";
    return "extra";
}

static void FormatVic2Date(int raw, char* buf, size_t bufsz)
{
    static const int kMDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int adj = raw - OOS_DATE_EPOCH;
    int year = 0;
    int month = 1;
    int day = 1;
    int hour = 0;
    if (adj >= 0)
    {
        year = adj / 8760;
        int rem = adj % 8760;
        hour = rem % 24;
        int doy = rem / 24;
        month = 1;
        for (int m = 0; m < 12; ++m)
        {
            if (doy < kMDays[m])
            {
                day = doy + 1;
                break;
            }
            doy -= kMDays[m];
            month++;
        }
        if (month > 12)
        {
            month = 12;
            day = 31;
        }
    }
    sprintf_s(buf, bufsz, "%04d-%02d-%02d %02d:00", year, month, day, hour);
}

static int CopyVecU32(void* vecObj, unsigned* out, int cap, int* outCount)
{
    *outCount = -1;
    unsigned begin = 0;
    unsigned end = 0;
    __try
    {
        begin = *(unsigned*)vecObj;
        end = *((unsigned*)vecObj + 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }

    if (!begin)
    {
        *outCount = 0;
        return 1;
    }
    if (end < begin)
        return 0;

    unsigned nbytes = end - begin;
    if (nbytes % 4)
        return 0;

    int n = (int)(nbytes / 4);
    *outCount = n;
    int copy = n;
    if (copy > cap)
        copy = cap;
    if (copy <= 0)
        return 1;

    __try
    {
        memcpy(out, (const void*)(DWORD_PTR)begin, (size_t)copy * 4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    return 1;
}

static void LogIdentDayTail()
{
    LONG total = IdentSkipTotal();
    LONG delta = total - g_identSkipLogged;
    g_identSkipLogged = total;
    char diag[320];
    FormatIdentDiag(diag, sizeof(diag));
    LogOosFile("  %s today_delta=%d", diag, (int)delta);
}

static void NoteOosMilestones(int isDiff, int realDiff, int raw, const char* buf)
{
    RememberSessionClock(raw, buf);
    if (isDiff && !g_firstOosRaw && raw)
    {
        g_firstOosRaw = raw;
        strcpy_s(g_firstOosBuf, buf);
        LogOosFile("FIRST OOS (любой DIFF, в т.ч. local=0) date=%s", buf);
    }
    if (realDiff && !g_firstRealOosRaw && raw)
    {
        g_firstRealOosRaw = raw;
        strcpy_s(g_firstRealOosBuf, buf);
        LogOosFile("FIRST real OOS (оба checksum ненулевые и разные) date=%s", buf);
    }
}

static void TryLogCStringField(const char* tag, void* p)
{
    char tmp[64];
    memset(tmp, 0, sizeof(tmp));
    int ok = 0;
    __try
    {
        const char* s = *(const char**)p;
        if (s)
        {
            memcpy(tmp, s, sizeof(tmp) - 1);
            ok = 1;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = 0;
    }
    if (!ok || tmp[0] < 32 || tmp[0] > 126)
        return;
    for (int i = 0; tmp[i]; ++i)
    {
        if ((unsigned char)tmp[i] < 32 || (unsigned char)tmp[i] > 126)
        {
            tmp[i] = 0;
            break;
        }
    }
    if (tmp[0])
        LogOosFile("  %s=\"%s\"", tag, tmp);
}

static void TryLogPeerCmd(void* vecAt3C)
{
    if (!vecAt3C)
        return;
    char tmp[32];
    memset(tmp, 0, sizeof(tmp));
    unsigned size = 0;
    unsigned cap = 0;
    __try
    {
        char* obj = (char*)vecAt3C - 0x3C;
        size = *(unsigned*)(obj + 0x1C);
        cap = *(unsigned*)(obj + 0x20);
        const char* s = obj + 8;
        if (cap >= 16)
            s = *(const char**)(obj + 8);
        if (s && size > 0 && size < sizeof(tmp))
            memcpy(tmp, s, size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    if (tmp[0] >= 32 && tmp[0] <= 126)
        LogOosFile("  cmd=\"%s\"", tmp);
}

static int LooksLikeTag(const char* p)
{
    unsigned char a = (unsigned char)p[0];
    unsigned char b = (unsigned char)p[1];
    unsigned char c = (unsigned char)p[2];
    if (a < 'A' || a > 'Z' || b < 'A' || b > 'Z')
        return 0;
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
        return 0;
    return 1;
}

static void FillTag(char* out, void* obj)
{
    out[0] = 0;
    if (!obj || SafeIsBadReadPtr(obj, 0x40))
        return;
    __try
    {
        static const int kOffs[] = { 4, 8, 0xC, 0x20, 0x24, 0x30, 0 };
        for (int k = 0; k < 7; ++k)
        {
            const char* p = (const char*)obj + kOffs[k];
            if (LooksLikeTag(p))
            {
                out[0] = p[0];
                out[1] = p[1];
                out[2] = p[2];
                out[3] = (p[3] >= 'A' && p[3] <= 'Z') ? p[3] : 0;
                out[4] = 0;
                return;
            }
            if (SafeIsBadReadPtr(p, 4))
                continue;
            void* q = *(void**)p;
            if (!q || SafeIsBadReadPtr(q, 4))
                continue;
            const char* t = (const char*)q;
            if (LooksLikeTag(t))
            {
                out[0] = t[0];
                out[1] = t[1];
                out[2] = t[2];
                out[3] = 0;
                return;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out[0] = 0;
    }
}

static void DumpLocalExtra(void* session, int verbose, int* outCount, unsigned* outXor)
{
    *outCount = -1;
    *outXor = 0;
    if (!session)
        return;

    unsigned begin = 0;
    unsigned end = 0;
    __try
    {
        begin = *(unsigned*)((char*)session + OOS_REC_OFF);
        end = *(unsigned*)((char*)session + OOS_REC_OFF + 4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }

    if (!begin || end < begin)
    {
        *outCount = 0;
        return;
    }

    unsigned nbytes = end - begin;
    if (nbytes % 16)
    {
        *outCount = -2;
        return;
    }

    int n = (int)(nbytes / 16);
    *outCount = n;
    int show = n;
    if (show > OOS_REC_MAX)
        show = OOS_REC_MAX;

    unsigned x = 0;
    for (int i = 0; i < show; ++i)
    {
        unsigned rec[4];
        memset(rec, 0, sizeof(rec));
        __try
        {
            memcpy(rec, (const void*)(DWORD_PTR)(begin + (unsigned)i * 16), 16);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            break;
        }
        x ^= rec[0] ^ rec[1] ^ rec[2] ^ rec[3];
        if (verbose)
        {
            char tag[8];
            FillTag(tag, (void*)(DWORD_PTR)rec[0]);
            LogOosFile("  rec[%d] ptr=%08X a=%08X b=%08X c=%08X tag=%s",
                i, rec[0], rec[1], rec[2], rec[3], tag[0] ? tag : "-");
        }
    }
    *outXor = x;
    if (verbose && n > OOS_REC_MAX)
        LogOosFile("  rec truncated to %d / %d", OOS_REC_MAX, n);

    if (verbose)
    {
        unsigned d[16];
        memset(d, 0, sizeof(d));
        __try
        {
            memcpy(d, (char*)session + 0xB00, sizeof(d));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        LogOosFile("  session+B00 %08X %08X %08X %08X %08X %08X %08X %08X",
            d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
        LogOosFile("  session+B20 %08X %08X %08X %08X %08X %08X %08X %08X",
            d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);

        void* p24 = 0;
        __try
        {
            p24 = *(void**)((char*)session + 0xB24);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            p24 = 0;
        }
        if (p24)
            DumpPtrLine("session+B24obj", p24);
    }
}

static void __cdecl ReportOos(void* a0, void* a1)
{

    unsigned int cw = 0;
    _controlfp_s(&cw, 0, 0);
    unsigned int mxcsr = _mm_getcsr();

    int fileCs = 0;
    int fileCsOk = 0;
    if (g_checksumAppPtr)
    {
        __try
        {
            fileCs = *(int*)((char*)g_checksumAppPtr + 0x30);
            fileCsOk = 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            fileCsOk = 0;
        }
    }

    int dateRaw = 0;
    int dateOk = 0;
    char dateBuf[32];
    dateBuf[0] = 0;
    if (a0)
    {
        __try
        {
            dateRaw = *(int*)((char*)a0 + OOS_DATE_OFF);
            dateOk = 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            dateOk = 0;
        }
    }
    if (dateOk)
        FormatVic2Date(dateRaw, dateBuf, sizeof(dateBuf));

    unsigned char oosFlag = 0;
    int oosFlagOk = 0;
    if (a0)
    {
        __try
        {
            oosFlag = *((unsigned char*)a0 + OOS_FLAG_OFF);
            oosFlagOk = 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            oosFlagOk = 0;
        }
    }

    unsigned localBuf[OOS_MAX_SLOTS];
    unsigned remoteBuf[OOS_MAX_SLOTS];
    memset(localBuf, 0, sizeof(localBuf));
    memset(remoteBuf, 0, sizeof(remoteBuf));
    int nLocal = -1;
    int nRemote = -1;
    int localOk = 0;
    int remoteOk = 0;
    if (a0)
        localOk = CopyVecU32((char*)a0 + OOS_VEC_OFF, localBuf, OOS_MAX_SLOTS, &nLocal);
    if (a1)
        remoteOk = CopyVecU32(a1, remoteBuf, OOS_MAX_SLOTS, &nRemote);

    int nCmp = 0;
    if (localOk && remoteOk && nLocal >= 0 && nRemote >= 0)
        nCmp = nLocal < nRemote ? nLocal : nRemote;
    if (nCmp > OOS_MAX_SLOTS)
        nCmp = OOS_MAX_SLOTS;

    int nDiff = 0;
    int firstDiff = -1;
    for (int i = 0; i < nCmp; ++i)
    {
        if (localBuf[i] != remoteBuf[i])
        {
            if (firstDiff < 0)
                firstDiff = i;
            ++nDiff;
        }
    }

    char summary[192];
    const int isDiff = (!localOk || !remoteOk || nLocal != nRemote || nDiff > 0);
    unsigned sumL = (localOk && nLocal > 0) ? localBuf[0] : 0;
    unsigned sumR = (remoteOk && nRemote > 0) ? remoteBuf[0] : 0;
    unsigned auxL = (localOk && nLocal > 1) ? localBuf[1] : 0;
    unsigned auxR = (remoteOk && nRemote > 1) ? remoteBuf[1] : 0;
    const int realDiff = isDiff && sumL > 0 && sumR > 0 && sumL != sumR;
    NoteOosMilestones(isDiff, realDiff, dateOk ? dateRaw : 0, dateOk ? dateBuf : "");

    if (!isDiff)
    {
        ++g_syncHits;
        int recN = -1;
        unsigned recXor = 0;
        DumpLocalExtra(a0, 0, &recN, &recXor);
        LogOosFile("SYNC n=%d date=%s sum=%u/%u aux=%u/%u rec=%d xor=%08X",
            g_syncHits, dateOk ? dateBuf : "?", sumL, sumR, auxL, auxR, recN, recXor);
        RememberChecksum("SYNC n=%d date=%s sum=%u/%u rec=%d xor=%08X",
            g_syncHits, dateOk ? dateBuf : "?", sumL, sumR, recN, recXor);
        LogIdentDayTail();
        return;
    }

    ++g_oosHits;

    if (!localOk || !remoteOk)
        sprintf_s(summary, "vectors unreadable local_ok=%d remote_ok=%d", localOk, remoteOk);
    else if (nLocal != nRemote)
        sprintf_s(summary, "COUNT mismatch local=%d remote=%d", nLocal, nRemote);
    else if (firstDiff >= 0)
        sprintf_s(summary, "%d/%d DIFF Checksum:%d %s local=%u remote=%u delta=%d",
            nDiff, nCmp, firstDiff, OosSlotLabel(firstDiff),
            localBuf[firstDiff], remoteBuf[firstDiff],
            (int)localBuf[firstDiff] - (int)remoteBuf[firstDiff]);
    else
        sprintf_s(summary, "%d/%d DIFF", nDiff, nCmp);

    RememberChecksum("OOS hit=%d after %d sync date=%s %s",
        g_oosHits, g_syncHits, dateOk ? dateBuf : "?", summary);

    Log("OOS[%d]: %s date=%s",
        g_oosHits, summary, dateOk ? dateBuf : "?");

    LogOosFile("OOS hit=%d after %d sync days dll=%s tick=%u date=%s raw=%d a0=%08X a1=%08X fpu_cw=%08X mxcsr=%08X",
        g_oosHits, g_syncHits, MOD_VERSION, GetTickCount(),
        dateOk ? dateBuf : "?", dateOk ? dateRaw : 0,
        (unsigned)(DWORD_PTR)a0, (unsigned)(DWORD_PTR)a1,
        cw, mxcsr);
    LogOosFile("  %s", summary);
    LogIdentDayTail();
    {
        int recN = -1;
        unsigned recXor = 0;
        DumpLocalExtra(a0, 1, &recN, &recXor);
        LogOosFile("  local +0xB84 rec=%d xor=%08X (в пакет не входит — сравни с таким же блоком у пира)",
            recN, recXor);
    }
    if (sumL == 0 && sumR > 100)
        LogOosFile("  NOTE: local sum=0 после ненулевого remote — локальный аккумулятор сброшен (часто хвост после уже показанного OOS)");
    if (oosFlagOk)
        LogOosFile("  session+0xB20 oos_flag=%u (в прологе ещё 0, флаг ставит сама функция)", (unsigned)oosFlag);
    if (fileCsOk)
        LogOosFile("  lobby_file_checksum=%d (%08X)  (это лобби-файлы, не lockstep)", fileCs, (unsigned)fileCs);

    if ((cw & _MCW_PC) != _PC_53)
        LogOosFile("  NOTE: FPU precision != 53-bit (cw=%08X) — D3D/оверлей мог сбить хеш", cw);
    if ((mxcsr & 0x8040) != 0x8040)
        LogOosFile("  NOTE: MXCSR без FTZ/DAZ (mxcsr=%08X)", mxcsr);

    DumpPtrLine("arg0", a0);
    DumpPtrLine("arg1", a1);
    if (a1)
    {
        void* peer = (char*)a1 - 0x3C;
        DumpPtrLine("peer", peer);
        TryLogPeerCmd(a1);
    }

    LogOosFile("  vector local @session+0xB74  count=%s%d  remote @peer+0x3C count=%s%d",
        localOk ? "" : "ERR ", nLocal,
        remoteOk ? "" : "ERR ", nRemote);
    LogOosFile("  --- slots (это ровно то, что сверяет диалог: Checksum: i local : remote) ---");

    if (!localOk && !remoteOk)
        LogOosFile("  (оба вектора не прочитались)");
    else
    {
        int nShow = nCmp;
        if (nLocal > nShow)
            nShow = nLocal;
        if (nRemote > nShow)
            nShow = nRemote;
        if (nShow > OOS_MAX_SLOTS)
            nShow = OOS_MAX_SLOTS;

        for (int i = 0; i < nShow; ++i)
        {
            const int haveL = localOk && i < nLocal && i < OOS_MAX_SLOTS;
            const int haveR = remoteOk && i < nRemote && i < OOS_MAX_SLOTS;
            if (haveL && haveR)
            {
                const int diff = localBuf[i] != remoteBuf[i];
                LogOosFile("  Checksum:%d %-8s  local=%d (%08X)  remote=%d (%08X)  %s",
                    i, OosSlotLabel(i),
                    (int)localBuf[i], localBuf[i],
                    (int)remoteBuf[i], remoteBuf[i],
                    diff ? "DIFF" : "MATCH");
            }
            else if (haveL)
            {
                LogOosFile("  Checksum:%d %-8s  local=%d (%08X)  remote=<missing>  DIFF",
                    i, OosSlotLabel(i), (int)localBuf[i], localBuf[i]);
            }
            else if (haveR)
            {
                LogOosFile("  Checksum:%d %-8s  local=<missing>  remote=%d (%08X)  DIFF",
                    i, OosSlotLabel(i), (int)remoteBuf[i], remoteBuf[i]);
            }
        }
        if ((localOk && nLocal > OOS_MAX_SLOTS) || (remoteOk && nRemote > OOS_MAX_SLOTS))
            LogOosFile("  (обрезано до %d слотов)", OOS_MAX_SLOTS);
    }
}

__declspec(naked) static void OosReportThunk()
{
    __asm {
        pushad
        mov eax, dword ptr [esp + 36]
        mov ecx, dword ptr [esp + 40]
        push ecx
        push eax
        call ReportOos
        add esp, 8
        popad
        push ebp
        mov ebp, esp
        push -1
        jmp dword ptr [g_oosResume]
    }
}

static bool InstallOosWatch()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_OOS_REPORT);
    if (hook[0] != 0x55 || hook[1] != 0x8B || hook[2] != 0xEC ||
        hook[3] != 0x6A || hook[4] != 0xFF)
    {
        Log("OosWatch: пролог не совпал (%02X %02X %02X %02X %02X)",
            hook[0], hook[1], hook[2], hook[3], hook[4]);
        return false;
    }
    if (memcmp(hook + 24, OOS_POST_SEH, sizeof(OOS_POST_SEH)) != 0)
    {
        Log("OosWatch: sub esp,0x140 не совпал");
        return false;
    }

    g_oosResume = g_base + RVA_OOS_REPORT + 5;

    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&OosReportThunk - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("OosWatch: FUN_00682EC0 rva %06X -> Logs\\v2dll_oos.log (checksum + ident_skip)", RVA_OOS_REPORT);
    LogOosFile("armed dll=%s (SYNC/OOS, ident_skip, days since first real OOS)", MOD_VERSION);
    return true;
}

// ---------------------------------------------------------------
// Стабильность движка: FPU, D3D, куча, TBB
//
// Цены в перехваченном UpdatePrice уже int64 (фикс. точка 2^15) —
// переводить их на float незачем. Деньги фабрик в дампах ещё double,
// x87 держит 80-битные промежуточные, а D3D9 без FPU_PRESERVE сбивает
// control word в 24 бита. Отсюда десинк между машинами с разным GPU
// и оверлеями.
//
// Параллель в exe уже есть: Intel TBB (task_scheduler_init). Второй
// пул поверх него не ставим — капим TBB на фиксированное N (ini) и
// пиним FPU на старте каждого потока. N одинаково у всех с этой DLL.
//
// LAA у текущего v2game.exe уже включён (Characteristics 0x20).
// ---------------------------------------------------------------

static const DWORD RVA_MAIN_LOOP = 0x5DF550;
static const unsigned char MAIN_LOOP_SIG[5] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
static const DWORD D3DCREATE_FPU_PRESERVE_FLAG = 0x00000002;
static const DWORD D3DPRESENT_INTERVAL_IMMEDIATE = 0x80000000;
static const int D3DPRESENT_INTERVAL_OFF = 52;
static const int D3D9_VT_CREATEDEVICE = 16;
static const int D3D9DEV_VT_RESET = 16;
static const int D3D9DEV_VT_PRESENT = 17;
static const char TBB_INIT_MANGLE[] = "?initialize@task_scheduler_init@tbb@@QAEXHI@Z";

static DWORD g_mainLoopResume = 0;
static int g_tbbMaxThreads = 8;

static void PinFpu()
{
    unsigned int ignored = 0;
    _controlfp_s(&ignored, _PC_53, _MCW_PC);
    _controlfp_s(&ignored, _RC_NEAR, _MCW_RC);
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}

static bool HookIat(HMODULE module, const char* dllName, const char* funcName, void* hook, void** orig)
{
    if (!module || !dllName || !funcName || !hook)
        return false;

    unsigned char* base = (unsigned char*)module;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva)
        return false;

    IMAGE_IMPORT_DESCRIPTOR* desc = (IMAGE_IMPORT_DESCRIPTOR*)(base + importRva);
    for (; desc->Name; ++desc)
    {
        const char* name = (const char*)(base + desc->Name);
        if (_stricmp(name, dllName) != 0)
            continue;

        IMAGE_THUNK_DATA32* thunk = (IMAGE_THUNK_DATA32*)(base + desc->FirstThunk);
        IMAGE_THUNK_DATA32* origThunk = desc->OriginalFirstThunk
            ? (IMAGE_THUNK_DATA32*)(base + desc->OriginalFirstThunk)
            : thunk;

        for (; thunk->u1.Function; ++thunk, ++origThunk)
        {
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
                continue;

            IMAGE_IMPORT_BY_NAME* byName = (IMAGE_IMPORT_BY_NAME*)(base + origThunk->u1.AddressOfData);
            if (strcmp((const char*)byName->Name, funcName) != 0)
                continue;

            DWORD* slot = (DWORD*)&thunk->u1.Function;
            if (*slot == (DWORD)(DWORD_PTR)hook)
                return true;

            HMODULE expected = GetModuleHandleA(dllName);
            if (expected)
            {
                MEMORY_BASIC_INFORMATION mbi;
                memset(&mbi, 0, sizeof(mbi));
                if (!VirtualQuery((void*)(DWORD_PTR)*slot, &mbi, sizeof(mbi)))
                    return false;
                // Непривязанный IAT держит RVA на имя в нашем модуле.
                // После bind указатель смотрит в чужой модуль (kernel32
                // часто форвардит в kernelbase — это нормально).
                if (mbi.AllocationBase == module)
                    return false;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProtect))
                return false;

            if (orig && !*orig)
                *orig = (void*)(DWORD_PTR)*slot;
            *slot = (DWORD)(DWORD_PTR)hook;
            VirtualProtect(slot, sizeof(DWORD), oldProtect, &oldProtect);
            return true;
        }
    }
    return false;
}

static bool PatchVtableSlot(void* obj, int slot, void* hook, void** orig)
{
    if (!obj || !hook)
        return false;

    void** vtable = *(void***)obj;
    if (!vtable)
        return false;

    if (vtable[slot] == hook)
        return true;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtable[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    if (orig && !*orig)
        *orig = vtable[slot];
    vtable[slot] = hook;
    VirtualProtect(&vtable[slot], sizeof(void*), oldProtect, &oldProtect);
    return true;
}

typedef DWORD (WINAPI* tGetTickCount)(void);
typedef HANDLE (WINAPI* tCreateThread)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
typedef DWORD_PTR (__cdecl *tBeginThreadEx)(void*, unsigned, LPTHREAD_START_ROUTINE, void*, unsigned, unsigned*);
typedef HMODULE (WINAPI* tLoadLibraryA)(LPCSTR);
typedef HMODULE (WINAPI* tLoadLibraryW)(LPCWSTR);
typedef void* (WINAPI* tDirect3DCreate9)(UINT);
typedef HRESULT (WINAPI* tD3D9CreateDevice)(void*, UINT, UINT, HWND, DWORD, void*, void**);
typedef HRESULT (WINAPI* tD3D9Reset)(void*, void*);
typedef HRESULT (WINAPI* tD3D9Present)(void*, const void*, const void*, HWND, const void*);

static void TryPatchLateModules();
static HMODULE WINAPI HookLoadLibraryA(LPCSTR name);
static HMODULE WINAPI HookLoadLibraryW(LPCWSTR name);

static tGetTickCount       g_realGetTickCount = 0;
static tCreateThread       g_realCreateThread = 0;
static tBeginThreadEx      g_realBeginThreadEx = 0;
static tLoadLibraryA       g_realLoadLibraryA = 0;
static tLoadLibraryW       g_realLoadLibraryW = 0;
static tDirect3DCreate9    g_realDirect3DCreate9 = 0;
static tD3D9CreateDevice   g_realCreateDevice = 0;
static tD3D9Reset          g_realReset = 0;
static tD3D9Present        g_realPresent = 0;
static void*               g_realTbbInit = 0;

static DWORD WINAPI HookGetTickCount(void)
{
    static LONG attempts = 0;
    if (attempts < 256)
    {
        InterlockedIncrement(&attempts);
        TryPatchLateModules();
    }
    return g_realGetTickCount ? g_realGetTickCount() : 0;
}

struct ThreadStartWrap
{
    LPTHREAD_START_ROUTINE orig;
    LPVOID param;
};

static DWORD WINAPI PinnedThreadStart(LPVOID raw)
{
    ThreadStartWrap* wrap = (ThreadStartWrap*)raw;
    LPTHREAD_START_ROUTINE orig = wrap->orig;
    LPVOID param = wrap->param;
    HeapFree(GetProcessHeap(), 0, wrap);
    PinFpu();
    return orig(param);
}

static HANDLE WINAPI HookCreateThread(
    LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE start,
    LPVOID param, DWORD flags, LPDWORD id)
{
    if (!g_realCreateThread)
        return 0;

    if (!start || start == PinnedThreadStart)
        return g_realCreateThread(sa, stack, start, param, flags, id);

    ThreadStartWrap* wrap = (ThreadStartWrap*)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ThreadStartWrap));
    if (!wrap)
        return g_realCreateThread(sa, stack, start, param, flags, id);

    wrap->orig = start;
    wrap->param = param;

    HANDLE h = g_realCreateThread(sa, stack, PinnedThreadStart, wrap, flags, id);
    if (!h)
        HeapFree(GetProcessHeap(), 0, wrap);
    return h;
}

static DWORD_PTR __cdecl HookBeginThreadEx(
    void* security, unsigned stack, LPTHREAD_START_ROUTINE start,
    void* arg, unsigned flags, unsigned* id)
{
    if (!g_realBeginThreadEx)
        return 0;

    if (!start || start == PinnedThreadStart)
        return g_realBeginThreadEx(security, stack, start, arg, flags, id);

    ThreadStartWrap* wrap = (ThreadStartWrap*)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ThreadStartWrap));
    if (!wrap)
        return g_realBeginThreadEx(security, stack, start, arg, flags, id);

    wrap->orig = start;
    wrap->param = arg;

    DWORD_PTR h = g_realBeginThreadEx(security, stack, PinnedThreadStart, wrap, flags, id);
    if (!h)
        HeapFree(GetProcessHeap(), 0, wrap);
    return h;
}

static HRESULT WINAPI HookPresent(void* device, const void* src, const void* dest, HWND wnd, const void* dirty)
{
    HRESULT hr = g_realPresent
        ? g_realPresent(device, src, dest, wnd, dirty)
        : E_FAIL;
    PinFpu();
    return hr;
}

static void ForceImmediatePresent(void* params)
{
    if (!params || !g_settings.patchD3dNoVsync)
        return;
    __try
    {
        *(DWORD*)((char*)params + D3DPRESENT_INTERVAL_OFF) = D3DPRESENT_INTERVAL_IMMEDIATE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static HRESULT WINAPI HookReset(void* self, void* params)
{
    ForceImmediatePresent(params);
    return g_realReset ? g_realReset(self, params) : E_FAIL;
}

static HRESULT WINAPI HookCreateDevice(
    void* self, UINT adapter, UINT type, HWND hwnd, DWORD flags,
    void* params, void** outDevice)
{
    if (g_settings.patchD3dFpuPreserve)
        flags |= D3DCREATE_FPU_PRESERVE_FLAG;
    ForceImmediatePresent(params);

    HRESULT hr = g_realCreateDevice
        ? g_realCreateDevice(self, adapter, type, hwnd, flags, params, outDevice)
        : E_FAIL;

    if (hr >= 0 && outDevice && *outDevice)
    {
        PatchVtableSlot(*outDevice, D3D9DEV_VT_RESET, (void*)HookReset, (void**)&g_realReset);
        if (PatchVtableSlot(*outDevice, D3D9DEV_VT_PRESENT, (void*)HookPresent, (void**)&g_realPresent))
            Log("D3D9: Present перехвачен, FPU_PRESERVE=%d noVsync=%d",
                (int)g_settings.patchD3dFpuPreserve,
                (int)g_settings.patchD3dNoVsync);
    }
    return hr;
}

static void* WINAPI HookDirect3DCreate9(UINT sdk)
{
    void* obj = g_realDirect3DCreate9 ? g_realDirect3DCreate9(sdk) : 0;
    if (obj)
        PatchVtableSlot(obj, D3D9_VT_CREATEDEVICE, (void*)HookCreateDevice, (void**)&g_realCreateDevice);
    return obj;
}

__declspec(naked) static void HookTbbInit()
{
    __asm {
        mov eax, g_tbbMaxThreads
        cmp eax, 1
        jl go_real
        mov dword ptr [esp + 4], eax
    go_real:
        jmp dword ptr [g_realTbbInit]
    }
}

static void HookTbbModule(HMODULE tbb)
{
    if (!tbb || !g_settings.patchThreadFpuPin)
        return;

    static LONG loggedCt = 0;
    static LONG loggedBt = 0;
    static LONG loggedFail = 0;

    bool ct = HookIat(tbb, "kernel32.dll", "CreateThread",
        (void*)HookCreateThread, (void**)&g_realCreateThread);
    bool bt = HookIat(tbb, "MSVCR100.dll", "_beginthreadex",
        (void*)HookBeginThreadEx, (void**)&g_realBeginThreadEx);

    if (ct && InterlockedCompareExchange(&loggedCt, 1, 0) == 0)
        Log("TBB: CreateThread перехвачен");
    if (bt && InterlockedCompareExchange(&loggedBt, 1, 0) == 0)
        Log("TBB: _beginthreadex перехвачен");
    if (!ct && !bt && InterlockedCompareExchange(&loggedFail, 1, 0) == 0)
        Log("TBB: ни CreateThread, ни _beginthreadex в IAT");
}

static void HookTbbInitOnExe()
{
    if (g_settings.engineWorkerThreads < 1)
        return;

    static LONG hooked = 0;
    if (hooked)
        return;

    HMODULE tbb = GetModuleHandleA("tbb.dll");
    if (!tbb)
        return;

    g_tbbMaxThreads = g_settings.engineWorkerThreads;
    if (!g_realTbbInit)
        g_realTbbInit = (void*)GetProcAddress(tbb, TBB_INIT_MANGLE);

    if (!g_realTbbInit)
    {
        Log("TBB: initialize не найден в tbb.dll");
        hooked = 1;
        return;
    }

    if (HookIat(GetModuleHandleA(NULL), "tbb.dll", TBB_INIT_MANGLE, (void*)HookTbbInit, &g_realTbbInit))
    {
        hooked = 1;
        Log("TBB: initialize капим на %d потоков", g_tbbMaxThreads);
    }
}

static void TryPatchLateModules()
{
    HMODULE exe = GetModuleHandleA(NULL);

    if (g_settings.patchThreadFpuPin)
    {
        HookIat(exe, "kernel32.dll", "CreateThread", (void*)HookCreateThread, (void**)&g_realCreateThread);
        HookIat(exe, "kernel32.dll", "LoadLibraryA", (void*)HookLoadLibraryA, (void**)&g_realLoadLibraryA);
        HookIat(exe, "kernel32.dll", "LoadLibraryW", (void*)HookLoadLibraryW, (void**)&g_realLoadLibraryW);
        HookIat(exe, "kernel32.dll", "GetTickCount", (void*)HookGetTickCount, (void**)&g_realGetTickCount);
    }

    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    static LONG d3dHooked = 0;
    if (g_settings.patchD3dFpuPreserve && d3d9 && d3dHooked == 0)
    {
        if (!g_realDirect3DCreate9)
            g_realDirect3DCreate9 = (tDirect3DCreate9)GetProcAddress(d3d9, "Direct3DCreate9");
        if (g_realDirect3DCreate9 &&
            HookIat(exe, "d3d9.dll", "Direct3DCreate9", (void*)HookDirect3DCreate9, (void**)&g_realDirect3DCreate9))
        {
            d3dHooked = 1;
            Log("D3D9: Direct3DCreate9 перехвачен");
        }
    }

    HMODULE tbb = GetModuleHandleA("tbb.dll");
    if (tbb)
    {
        HookTbbModule(tbb);
        HookTbbInitOnExe();
    }
}

static bool NameHasTbbA(const char* name)
{
    if (!name)
        return false;
    for (const char* p = name; *p; ++p)
    {
        if ((p[0] == 't' || p[0] == 'T') &&
            (p[1] == 'b' || p[1] == 'B') &&
            (p[2] == 'b' || p[2] == 'B'))
            return true;
    }
    return false;
}

static bool NameHasTbbW(const wchar_t* name)
{
    if (!name)
        return false;
    for (const wchar_t* p = name; *p; ++p)
    {
        if ((p[0] == L't' || p[0] == L'T') &&
            (p[1] == L'b' || p[1] == L'B') &&
            (p[2] == L'b' || p[2] == L'B'))
            return true;
    }
    return false;
}

static HMODULE WINAPI HookLoadLibraryA(LPCSTR name)
{
    HMODULE m = g_realLoadLibraryA ? g_realLoadLibraryA(name) : 0;
    if (m && NameHasTbbA(name))
        HookTbbModule(m);
    return m;
}

static HMODULE WINAPI HookLoadLibraryW(LPCWSTR name)
{
    HMODULE m = g_realLoadLibraryW ? g_realLoadLibraryW(name) : 0;
    if (m && NameHasTbbW(name))
        HookTbbModule(m);
    return m;
}

__declspec(naked) static void MainLoopThunk()
{
    __asm {
        pushad
        call PinFpu
        call TryPatchLateModules
        popad
        push ebp
        mov ebp, esp
        push -1
        jmp dword ptr [g_mainLoopResume]
    }
}

static bool InstallMainLoopFpuPin()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_MAIN_LOOP);
    if (memcmp(hook, MAIN_LOOP_SIG, sizeof(MAIN_LOOP_SIG)) != 0)
    {
        Log("FPU: сигнатура главного цикла не совпала (%02X %02X %02X %02X %02X)",
            hook[0], hook[1], hook[2], hook[3], hook[4]);
        return false;
    }

    g_mainLoopResume = g_base + RVA_MAIN_LOOP + 5;

    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&MainLoopThunk - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("FPU: пин на главном цикле rva %06X", RVA_MAIN_LOOP);
    return true;
}

static void InstallHeapLfh()
{
    HANDLE heaps[128];
    DWORD n = GetProcessHeaps(128, heaps);
    DWORD lfh = 2;
    DWORD ok = 0;
    for (DWORD i = 0; i < n; ++i)
    {
        if (HeapSetInformation(heaps[i], (HEAP_INFORMATION_CLASS)0, &lfh, sizeof(lfh)))
            ++ok;
    }
    Log("Heap: LFH включён на %u из %u куч (LAA exe=%d)",
        ok, n,
        (((IMAGE_NT_HEADERS32*)((unsigned char*)g_base +
            ((IMAGE_DOS_HEADER*)g_base)->e_lfanew))->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) ? 1 : 0);
}

static const DWORD RVA_SLEEP_IAT = 0x88A0EC;
static const DWORD RVA_MAIN_SLEEP_A = 0x5DF2D5;
static const DWORD RVA_MAIN_SLEEP_B = 0x5DF684;

static bool PatchImm8Sleep(DWORD rvaPush, unsigned char expectMs, unsigned char newMs, const char* tag)
{
    unsigned char* p = (unsigned char*)(g_base + rvaPush);
    DWORD iat = g_base + RVA_SLEEP_IAT;
    if (p[0] != 0x6A || p[2] != 0xFF || p[3] != 0x15 || *(DWORD*)(p + 4) != iat)
    {
        Log("%s: сигнатура Sleep не совпала rva %06X (%02X %02X %02X %02X)",
            tag, rvaPush, p[0], p[1], p[2], p[3]);
        return false;
    }
    if (p[1] != expectMs && p[1] != newMs)
    {
        Log("%s: неожиданный imm Sleep(%u) rva %06X", tag, (unsigned)p[1], rvaPush);
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    p[1] = newMs;
    VirtualProtect(p, 2, oldProtect, &oldProtect);
    Log("%s: Sleep(%u) -> Sleep(%u) rva %06X", tag, (unsigned)expectMs, (unsigned)newMs, rvaPush);
    return true;
}

static void PatchPushImm8(unsigned char* p, unsigned char newMs)
{
    if (p[0] != 0x6A)
        return;
    DWORD oldProtect = 0;
    if (!VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
        return;
    p[1] = newMs;
    VirtualProtect(p, 2, oldProtect, &oldProtect);
}

static void InstallMainLoopSleep0()
{
    // 6A 00 EB 02 6A 64 FF 15 [Sleep IAT] — ветка Sleep(0) vs Sleep(100).
    int ms = g_settings.mainLoopSleepMs;
    if (ms < 0)
        ms = 0;
    if (ms > 127)
        ms = 127;

    unsigned char* a = (unsigned char*)(g_base + RVA_MAIN_SLEEP_A);
    unsigned char* b = (unsigned char*)(g_base + RVA_MAIN_SLEEP_B);
    if (a[-4] != 0x6A || a[-3] != 0x00 || a[-2] != 0xEB || a[-1] != 0x02 ||
        b[-4] != 0x6A || b[-3] != 0x00 || b[-2] != 0xEB || b[-1] != 0x02)
    {
        Log("MainLoopSleep0: префикс 6A 00 EB 02 не совпал");
        return;
    }
    PatchPushImm8(a - 4, (unsigned char)ms);
    PatchPushImm8(b - 4, (unsigned char)ms);
    PatchImm8Sleep(RVA_MAIN_SLEEP_A, 100, (unsigned char)ms, "MainLoopSleep0");
    PatchImm8Sleep(RVA_MAIN_SLEEP_B, 100, (unsigned char)ms, "MainLoopSleep0");
}

static void InstallHighPriority()
{
    if (SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS))
        Log("CPU: priority ABOVE_NORMAL");
    else
        Log("CPU: SetPriorityClass failed %u", GetLastError());

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32)
        return;

    typedef BOOL (WINAPI* tSetProcessInformation)(HANDLE, int, LPVOID, DWORD);
    tSetProcessInformation fn = (tSetProcessInformation)GetProcAddress(k32, "SetProcessInformation");
    if (!fn)
        return;

    struct PowerThrottle
    {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    } state;
    state.Version = 1;
    state.ControlMask = 0x1; // PROCESS_POWER_THROTTLING_EXECUTION_SPEED
    state.StateMask = 0;
    if (fn(GetCurrentProcess(), 4, &state, sizeof(state)))
        Log("CPU: power throttling off");
}

static bool InstallEngineStability()
{
    g_fnIsBadReadPtr = SafeIsBadReadPtr;
    PinFpu();

    HMODULE exe = GetModuleHandleA(NULL);

    if (g_settings.patchHighPriority)
        InstallHighPriority();

    if (g_settings.patchHeapLfh)
        InstallHeapLfh();

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (g_settings.patchThreadFpuPin && kernel32)
    {
        g_realCreateThread = (tCreateThread)GetProcAddress(kernel32, "CreateThread");
        g_realLoadLibraryA = (tLoadLibraryA)GetProcAddress(kernel32, "LoadLibraryA");
        g_realLoadLibraryW = (tLoadLibraryW)GetProcAddress(kernel32, "LoadLibraryW");
        g_realGetTickCount = (tGetTickCount)GetProcAddress(kernel32, "GetTickCount");

        if (HookIat(exe, "kernel32.dll", "CreateThread", (void*)HookCreateThread, (void**)&g_realCreateThread))
            Log("FPU: CreateThread exe перехвачен");
        else
            Log("FPU: CreateThread IAT не найден");

        HookIat(exe, "kernel32.dll", "LoadLibraryA", (void*)HookLoadLibraryA, (void**)&g_realLoadLibraryA);
        HookIat(exe, "kernel32.dll", "LoadLibraryW", (void*)HookLoadLibraryW, (void**)&g_realLoadLibraryW);
        if (HookIat(exe, "kernel32.dll", "GetTickCount", (void*)HookGetTickCount, (void**)&g_realGetTickCount))
            Log("FPU: GetTickCount - отложенный патч TBB/D3D");
    }

    if (g_settings.patchFpuFortress)
        InstallMainLoopFpuPin();

    if (g_settings.patchMainLoopSleep0)
        InstallMainLoopSleep0();

    TryPatchLateModules();

    Log("Engine: FPU=%d D3D=%d LFH=%d threadPin=%d tbbCap=%d sleep0=%d noVsync=%d prio=%d",
        (int)g_settings.patchFpuFortress,
        (int)g_settings.patchD3dFpuPreserve,
        (int)g_settings.patchHeapLfh,
        (int)g_settings.patchThreadFpuPin,
        g_settings.engineWorkerThreads,
        (int)g_settings.patchMainLoopSleep0,
        (int)g_settings.patchD3dNoVsync,
        (int)g_settings.patchHighPriority);
    return true;
}

// ---------------------------------------------------------------
// Квантование POP после дневного прохода FUN_00485E40
//
// Сами поля CPop уже int64 с 15 дробными битами (не IEEE float).
// 0x485E40 — координатор: три раза зовёт FUN_00484F90, где пишутся
// money (+0x180) и savings (+0x250). После возврата обходим аргумент
// как CPop / вектор / список и срезаем младшие биты.
// ---------------------------------------------------------------

static const DWORD RVA_POP_DAILY = 0x85E40;
static const unsigned char POP_DAILY_SIG[9] =
    { 0x55, 0x8B, 0xEC, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const int POP_ID_TYPE = 46;
static const int POP_STRIDE = 0x2A8;
static const int POP_QUANTIZE_OFFS[] =
{
    0x118, 0x120, 0x128, 0x130, 0x138, 0x140,
    0x180, 0x1B0, 0x1C8,
    0x1D8, 0x1E0, 0x1E8, 0x1F0, 0x1F8, 0x200, 0x208, 0x210, 0x218,
    0x250
};
static const int POP_QUANTIZE_OFFS_N = sizeof(POP_QUANTIZE_OFFS) / sizeof(POP_QUANTIZE_OFFS[0]);

static DWORD g_popDailyRet = 0;
static void* g_popDailyArg0 = 0;
static void* g_popDailyEcx = 0;
static void* g_popDailyTramp = 0;

static void QuantizeFixed15(void* slot)
{
    int keep = g_settings.popQuantizeKeepBits;
    if (keep >= 15)
        return;
    if (keep < 0)
        keep = 0;

    unsigned drop = (unsigned)(15 - keep);
    __int64 v = *(__int64*)slot;
    __int64 bias = 1i64 << (drop - 1);
    if (v >= 0)
        v = ((v + bias) >> drop) << drop;
    else
        v = ((v - bias) >> drop) << drop;
    *(__int64*)slot = v;
}

static bool LooksLikePop(void* p)
{
    if (!p)
        return false;
    __try
    {
        return *(int*)((char*)p + 8) == POP_ID_TYPE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static void QuantizeOnePop(void* pop)
{
    if (!LooksLikePop(pop))
        return;
    __try
    {
        for (int i = 0; i < POP_QUANTIZE_OFFS_N; ++i)
            QuantizeFixed15((char*)pop + POP_QUANTIZE_OFFS[i]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void QuantizePopObject(void* obj)
{
    if (!obj)
        return;

    if (LooksLikePop(obj))
    {
        int n = 0;
        void* p = obj;
        while (p && n < 400000)
        {
            QuantizeOnePop(p);
            ++n;
            __try { p = *(void**)((char*)p + 0x27C); }
            __except (EXCEPTION_EXECUTE_HANDLER) { break; }
            if (p == obj)
                break;
        }
        return;
    }

    void** begin = 0;
    void** end = 0;
    __try
    {
        begin = *(void***)((char*)obj + 0x44);
        end = *(void***)((char*)obj + 0x48);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }

    if (!begin || !end || end < begin)
        return;

    int count = (int)(end - begin);
    if (count > 0 && count < 500000 && LooksLikePop(*begin))
    {
        for (void** it = begin; it < end; ++it)
            QuantizeOnePop(*it);
        return;
    }

    int bytes = (int)((char*)end - (char*)begin);
    if (bytes >= POP_STRIDE && (bytes % POP_STRIDE) == 0 && bytes / POP_STRIDE < 500000
        && LooksLikePop(begin))
    {
        char* p = (char*)begin;
        char* e = (char*)end;
        for (; p < e; p += POP_STRIDE)
            QuantizeOnePop(p);
    }
}

static void AfterPopDaily()
{
    PinFpu();
    QuantizePopObject(g_popDailyArg0);
    if (g_popDailyEcx != g_popDailyArg0)
        QuantizePopObject(g_popDailyEcx);
}

__declspec(naked) static void PopDailyAfterThunk()
{
    __asm {
        pushad
        call AfterPopDaily
        popad
        jmp dword ptr [g_popDailyRet]
    }
}

__declspec(naked) static void PopDailyEntryThunk()
{
    __asm {
        mov dword ptr [g_popDailyEcx], ecx
        mov eax, dword ptr [esp + 4]
        mov dword ptr [g_popDailyArg0], eax
        pop dword ptr [g_popDailyRet]
        push offset PopDailyAfterThunk
        jmp dword ptr [g_popDailyTramp]
    }
}

static bool InstallPopQuantize()
{
    if (g_settings.popQuantizeKeepBits >= 15)
    {
        Log("PopQuantize: KEEP_BITS>=15 - квантование выключено");
        return false;
    }

    unsigned char* hook = (unsigned char*)(g_base + RVA_POP_DAILY);
    if (memcmp(hook, POP_DAILY_SIG, sizeof(POP_DAILY_SIG)) != 0)
    {
        Log("PopQuantize: сигнатура FUN_00485E40 не совпала");
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!cave)
        return false;

    memcpy(cave, hook, 9);
    cave[9] = 0xE9;
    *(DWORD*)(cave + 10) = (DWORD)(hook + 9) - (DWORD)(cave + 14);
    g_popDailyTramp = cave;

    unsigned char patch[9];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)(DWORD_PTR)&PopDailyEntryThunk - ((DWORD)hook + 5);
    memset(patch + 5, 0x90, 4);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("PopQuantize: FUN_00485E40, keep_bits=%d", g_settings.popQuantizeKeepBits);
    return true;
}

// ---------------------------------------------------------------
// MP-клиент: message pump со Sleep(40) → ~25 FPS (хост ~50).
// Совпадает с многолетними жалобами: просадка уже в лобби, не от
// симуляции. Единственный 6A 28 + call Sleep в exe: rva 0x71DD2C.
// TBB здесь ни при чём: Sleep(35)/Sleep(30) около 00A63Bxx — тайминг
// с double, похоже на звук; task_scheduler_init капится отдельно.
// ---------------------------------------------------------------

static const DWORD RVA_MP_CLIENT_SLEEP = 0x71DD2C;

static bool InstallMpClientSleep()
{
    int ms = g_settings.mpClientSleepMs;
    if (ms < 0)
        ms = 0;
    if (ms > 127)
        ms = 127;

    return PatchImm8Sleep(RVA_MP_CLIENT_SLEEP, 40, (unsigned char)ms, "MpClientSleep");
}

static bool Install()
{
    LoadSettings();

    g_base = (DWORD)GetModuleHandleA(NULL);
    if (!g_base)
        return false;

    g_imageSize = 0;
    {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_base;
        if (dos->e_magic == IMAGE_DOS_SIGNATURE)
        {
            IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(g_base + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                g_imageSize = nt->OptionalHeader.SizeOfImage;
        }
        if (!g_imageSize)
            g_imageSize = 0xC00000;
    }

    g_fnOnMakeDecision = (void*)(g_base + RVA_ONMAKEDECISION);

    Log("---- Install ---- версия %s", MOD_VERSION);
    Log("base = %08X", g_base);
    Log("logs = Logs\\ (v2dll.log, v2dll_oos.log, v2dll_crash.log)");

    InstallEngineStability();

    if (g_settings.patchPopQuantize)
        InstallPopQuantize();

    if (g_settings.patchMpClientSleep)
        InstallMpClientSleep();

    // Поддельные элементы: "POLITICSVIEW_DECISION" + имя решения.
    memset(g_fakeElem, 0, sizeof(g_fakeElem));

    for (int i = 0; i < BUTTON_COUNT && i < MAX_BUTTONS; ++i)
    {
        strcpy_s(g_decisionText[i], sizeof(g_decisionText[i]),
            "POLITICSVIEW_DECISION");
        strcat_s(g_decisionText[i], sizeof(g_decisionText[i]),
            BUTTONS[i].decision);

        *(char**)(g_fakeElem[i] + ELEM_STRDATA) = g_decisionText[i];
        *(unsigned*)(g_fakeElem[i] + ELEM_STRRES) = sizeof(g_decisionText[i]) - 1;
    }

    for (int i = 0; i < VIEW_COUNT && i < MAX_VIEWS; ++i)
    {
        void* thunk = VIEWS[i].tooltipSlot ? TOOLTIP_THUNKS[i] : UPDATE_THUNKS[i];

        bool ok = PatchSlot(VIEWS[i].rvaVtable, VIEWS[i].slot,
            thunk, &g_origSlot[i]);

        Log("patch %s: слот %d = %d", VIEWS[i].name, VIEWS[i].slot, (int)ok);
    }

    if (g_settings.decisionFilter)
    {
        bool ok = PatchSlot(RVA_VTABLE_DECISION, VT_SLOT_ISVALID,
            (void*)&MyDecisionIsValid, (void**)&g_origIsValid);
        Log("patch CDecision: слот %d = %d", VT_SLOT_ISVALID, (int)ok);
    }

    // Каждая запись таблицы уважает своё собственное BytePatch::enabled
    // (правится ключами PATCH_<ИМЯ> в ini), поэтому вызов сам по себе
    // безусловный.
    InstallExePatches();

    if (g_settings.patchOccupiedReinforceSplit)
        InstallOccupiedReinforceSplit();

    if (g_settings.patchAllyOwnerCheck)
        InstallAllyOwnerCheck();

    if (g_settings.patchCivilizeNullCheck)
        InstallCivilizeNullCheck();

    if (g_settings.patchNullVtableUi)
    {
        InstallNullVtableUi();
        InstallIdentityNullVtable();
        InstallIdentityLookupGuard();
        InstallIdentitySlot84();
        InstallIdentityFieldE4();
        InstallIdentityFnD1BB0();
        InstallIdentityFnD4420();
        InstallIdentityFn113D50();
        InstallStringClearGuard();
    }

    if (g_settings.patchGraphPointClamp)
        InstallGraphPointClamp();

    if (g_settings.patchFactoryDumpScan)
    {
        // В 2.58 сканер временно ловил "Failed to create a graphics device"
        // при запуске - подтверждено A/B тестом (2.58 с широким сканом всех
        // типов памяти ловил ошибку, 2.59 без вызова этой функции - нет).
        // Причина сужена до расширения скана за пределы MEM_PRIVATE (кучи
        // процесса) на MEM_IMAGE/MEM_MAPPED, где могли жить внутренние
        // данные видеодрайвера - в 2.60 это ограничение возвращено, снова
        // сканируем только MEM_PRIVATE, как в безотказных 2.55-2.57.
        InstallFactoryDumpScan();
    }

    if (g_settings.patchProdListVisibility)
        InstallProdListVisibilityHook();

    if (g_settings.patchProdTypeGate)
        InstallProdTypeGateHook();

    // Оба патча целят один и тот же адрес - взаимоисключающе.
    if (g_settings.priceDelta && g_settings.patchExponentialPriceDelta)
        Log("PriceDelta: ENABLE_PRICE_DELTA и PATCH_EXPONENTIAL_PRICE_DELTA "
            "патчат один адрес - применяется только PATCH_EXPONENTIAL_PRICE_DELTA");

    if (g_settings.patchExponentialPriceDelta)
        InstallExponentialPriceDelta();
    else if (g_settings.priceDelta)
        InstallPriceDelta();

    if (g_settings.popDisplay)
        InstallPopDisplay();

    if (g_settings.versionLabel)
        InstallVersionLabel();

    if (g_settings.patchCombatRoll)
        InstallCombatRoll();

    if (g_settings.patchChecksumDiagnostic)
    {
        InstallChecksumDiagnostic();
        InstallLobbyEntryHook();
    }

    if (g_settings.enableOosLog)
        InstallOosWatch();

    Log("Install: done");
    return true;
}


// ---------------------------------------------------------------
// Краш: необработанное исключение / abort → Logs\v2dll_crash.log
// и Logs\v2dll_crash_YYYYMMDD_HHMMSS_pid_tid_n.dmp (каждый отдельно).
// Если v2dll_crash.log занят — тот же штамп .log рядом.
// VectoredContinue + UEF: игра может сама поставить фильтр после нас,
// поэтому IAT SetUnhandledExceptionFilter оставляем обёрткой.
// Не глотаем исключение — Windows Error Reporting как был.
// ---------------------------------------------------------------

static LPTOP_LEVEL_EXCEPTION_FILTER g_prevUef = 0;
typedef LPTOP_LEVEL_EXCEPTION_FILTER (WINAPI* tSetUnhandledExceptionFilter)(LPTOP_LEVEL_EXCEPTION_FILTER);
static tSetUnhandledExceptionFilter g_realSetUEF = 0;
static LONG g_inCrash = 0;

static const char* CrashCodeName(DWORD code)
{
    switch (code)
    {
    case 0xC0000005: return "ACCESS_VIOLATION";
    case 0xC0000006: return "IN_PAGE_ERROR";
    case 0xC0000008: return "INVALID_HANDLE";
    case 0xC000001D: return "ILLEGAL_INSTRUCTION";
    case 0xC0000025: return "NONCONTINUABLE";
    case 0xC0000026: return "INVALID_DISPOSITION";
    case 0xC000008C: return "ARRAY_BOUNDS_EXCEEDED";
    case 0xC000008D: return "FLOAT_DENORMAL";
    case 0xC000008E: return "FLOAT_DIVIDE_BY_ZERO";
    case 0xC000008F: return "FLOAT_INEXACT";
    case 0xC0000090: return "FLOAT_INVALID";
    case 0xC0000091: return "FLOAT_OVERFLOW";
    case 0xC0000092: return "FLOAT_STACK_CHECK";
    case 0xC0000093: return "FLOAT_UNDERFLOW";
    case 0xC0000094: return "INTEGER_DIVIDE_BY_ZERO";
    case 0xC0000096: return "PRIVILEGED_INSTRUCTION";
    case 0xC00000FD: return "STACK_OVERFLOW";
    case 0xC0000135: return "DLL_NOT_FOUND";
    case 0xC0000139: return "ENTRYPOINT_NOT_FOUND";
    case 0xC0000142: return "DLL_INIT_FAILED";
    case 0xC0000374: return "HEAP_CORRUPTION";
    case 0xC0000409: return "STACK_BUFFER_OVERRUN";
    case 0xC0000417: return "INVALID_CRUNTIME_PARAMETER";
    case 0x40000015: return "FATAL_APP_EXIT/abort";
    case 0x80000003: return "BREAKPOINT";
    case 0x80000004: return "SINGLE_STEP";
    case 0xE06D7363: return "CPP_EXCEPTION";
    default:         return "UNKNOWN";
    }
}

static void CrashWrite(HANDLE h, const char* s)
{
    if (!h || h == INVALID_HANDLE_VALUE || !s)
        return;
    DWORD n = 0;
    WriteFile(h, s, (DWORD)strlen(s), &n, NULL);
}

static void CrashPrintf(HANDLE h, const char* fmt, ...)
{
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0)
        CrashWrite(h, buf);
}

static void CrashLogAddr(HANDLE h, const char* tag, DWORD addr)
{
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    char mod[MAX_PATH];
    mod[0] = 0;
    if (addr && VirtualQuery((const void*)(DWORD_PTR)addr, &mbi, sizeof(mbi)) && mbi.AllocationBase)
    {
        GetModuleFileNameA((HMODULE)mbi.AllocationBase, mod, sizeof(mod));
        const char* base = strrchr(mod, '\\');
        DWORD rva = addr - (DWORD)(DWORD_PTR)mbi.AllocationBase;
        CrashPrintf(h, "  %s=%08X  %s+0x%X\n", tag, addr, base ? base + 1 : mod, rva);
        return;
    }
    CrashPrintf(h, "  %s=%08X\n", tag, addr);
}

static void CrashDumpPtr(HANDLE h, const char* tag, DWORD addr)
{
    if (!addr)
    {
        CrashPrintf(h, "  %s=00000000\n", tag);
        return;
    }
    if (SafeIsBadReadPtr((const void*)(DWORD_PTR)addr, 32))
    {
        CrashPrintf(h, "  %s=%08X unreadable\n", tag, addr);
        return;
    }

    unsigned d[8];
    memset(d, 0, sizeof(d));
    __try
    {
        memcpy(d, (const void*)(DWORD_PTR)addr, sizeof(d));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashPrintf(h, "  %s=%08X faulted\n", tag, addr);
        return;
    }

    CrashPrintf(h, "  %s=%08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
        tag, addr, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
}

static void CrashDumpNested(HANDLE h, const char* tag, DWORD base, int off)
{
    if (!base || SafeIsBadReadPtr((const void*)(DWORD_PTR)(base + (DWORD)off), 4))
        return;
    DWORD inner = 0;
    __try
    {
        inner = *(DWORD*)(DWORD_PTR)(base + (DWORD)off);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    char nested[40];
    sprintf_s(nested, "[%s+%X]", tag, (unsigned)off);
    CrashDumpPtr(h, nested, inner);
}

static void CrashDumpCode(HANDLE h, DWORD eip)
{
    if (!eip)
        return;
    DWORD start = (eip > 8) ? (eip - 8) : eip;
    unsigned char b[24];
    memset(b, 0, sizeof(b));
    if (SafeIsBadReadPtr((const void*)(DWORD_PTR)start, sizeof(b)))
    {
        CrashPrintf(h, "  code_at_eip unreadable\n");
        return;
    }
    __try
    {
        memcpy(b, (const void*)(DWORD_PTR)start, sizeof(b));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashPrintf(h, "  code_at_eip faulted\n");
        return;
    }
    CrashPrintf(h,
        "  code_eip-8 %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

// Улики «откуда краш» без Ghidra: RTTI живых объектов, ключи
// локализации/протокола на стеке, vtable в образе exe или куча.
// Это не сюжет («ход спросил титул»), а то, что раньше руками
// вытаскивали из minidump / .rdata. Сюжет по-прежнему пишет человек.
static int CrashInImage(DWORD p)
{
    return g_base && p >= g_base && p < g_base + g_imageSize;
}

static int CrashSeenAdd(DWORD* seen, int* n, int cap, DWORD v)
{
    if (!v)
        return 0;
    for (int i = 0; i < *n; ++i)
    {
        if (seen[i] == v)
            return 0;
    }
    if (*n >= cap)
        return 0;
    seen[(*n)++] = v;
    return 1;
}

static int CrashCopyKey(DWORD addr, char* out, int outCap)
{
    out[0] = 0;
    if (!addr || outCap < 8)
        return 0;
    char tmp[64];
    memset(tmp, 0, sizeof(tmp));
    __try
    {
        memcpy(tmp, (const void*)(DWORD_PTR)addr, sizeof(tmp) - 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    int n = 0;
    int letters = 0;
    for (; n < (int)sizeof(tmp) - 1; ++n)
    {
        unsigned char c = (unsigned char)tmp[n];
        if (c == 0)
            break;
        if (c == '$' || c == '_' ||
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9'))
        {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                ++letters;
            continue;
        }
        return 0;
    }
    if (n < 4 || letters < 3)
        return 0;
    if (n >= outCap)
        n = outCap - 1;
    memcpy(out, tmp, (size_t)n);
    out[n] = 0;
    return n;
}

static void CrashCopyNear(DWORD addr, char* out, int outCap)
{
    out[0] = 0;
    if (!CrashInImage(addr) || outCap < 8)
        return;
    char raw[80];
    memset(raw, 0, sizeof(raw));
    __try
    {
        memcpy(raw, (const void*)(DWORD_PTR)addr, sizeof(raw) - 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    int w = 0;
    int gap = 0;
    for (int i = 0; i < (int)sizeof(raw) - 1 && w < outCap - 1; ++i)
    {
        unsigned char c = (unsigned char)raw[i];
        if (c >= 32 && c < 127)
        {
            out[w++] = (char)c;
            gap = 0;
        }
        else if (w > 0 && gap == 0)
        {
            out[w++] = ' ';
            gap = 1;
        }
    }
    while (w > 0 && out[w - 1] == ' ')
        --w;
    out[w] = 0;
}

static int CrashCopyRtti(DWORD obj, char* out, int outCap)
{
    out[0] = 0;
    if (!obj)
        return 0;
    DWORD vptr = 0;
    __try
    {
        vptr = *(DWORD*)(DWORD_PTR)obj;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    if (!CrashInImage(vptr) || vptr < 4)
        return 0;
    DWORD col = 0;
    __try
    {
        col = *(DWORD*)(DWORD_PTR)(vptr - 4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    if (!CrashInImage(col))
        return 0;
    DWORD td = 0;
    __try
    {
        td = *(DWORD*)(DWORD_PTR)(col + 12);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    if (!CrashInImage(td))
        return 0;
    char raw[80];
    memset(raw, 0, sizeof(raw));
    __try
    {
        memcpy(raw, (const void*)(DWORD_PTR)(td + 8), sizeof(raw) - 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
    const char* s = raw;
    if (s[0] == '.' && s[1] == '?' && s[2] == 'A')
        s += 4;
    if (s[0] == '?' && s[1] == '$')
        return 0;
    int n = 0;
    while (s[n] && s[n] != '@' && n < 64)
        ++n;
    if (n < 2)
        return 0;
    if (n >= outCap)
        n = outCap - 1;
    memcpy(out, s, (size_t)n);
    out[n] = 0;
    return n;
}

static const char* CrashVptrKind(DWORD obj, DWORD* outVptr)
{
    *outVptr = 0;
    if (!obj)
        return "null";
    DWORD vptr = 0;
    __try
    {
        vptr = *(DWORD*)(DWORD_PTR)obj;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return "unreadable";
    }
    *outVptr = vptr;
    if (!vptr)
        return "null-vtable";
    if (CrashInImage(vptr))
        return "vtable-in-exe";
    DWORD probe = 0;
    __try
    {
        probe = *(DWORD*)(DWORD_PTR)vptr;
        (void)probe;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return "dangling-vtable";
    }
    return "vtable-outside-exe";
}

static void CrashTryKey(HANDLE h, DWORD addr, DWORD* seen, int* nseen)
{
    if (*nseen >= 16)
        return;
    char key[48];
    if (!CrashCopyKey(addr, key, sizeof(key)))
        return;
    if (!CrashSeenAdd(seen, nseen, 16, addr))
        return;
    if (CrashInImage(addr))
    {
        char nearBuf[80];
        CrashCopyNear(addr, nearBuf, sizeof(nearBuf));
        CrashPrintf(h, "    %s  va=%08X  near=%s\n", key, addr, nearBuf);
    }
    else
        CrashPrintf(h, "    %s  va=%08X  (heap)\n", key, addr);
}

static void CrashTryObject(HANDLE h, const char* tag, DWORD addr, DWORD* seen, int* nseen, int fromStack)
{
    if (*nseen >= 16)
        return;
    DWORD vptr = 0;
    const char* kind = CrashVptrKind(addr, &vptr);
    char rtti[48];
    rtti[0] = 0;
    int hasRtti = CrashCopyRtti(addr, rtti, sizeof(rtti));
    if (fromStack && !hasRtti)
        return;
    if (!CrashSeenAdd(seen, nseen, 16, addr))
        return;
    if (hasRtti)
        CrashPrintf(h, "    %s=%08X  %s  vptr=%08X  %s\n", tag, addr, rtti, vptr, kind);
    else
        CrashPrintf(h, "    %s=%08X  vptr=%08X  %s\n", tag, addr, vptr, kind);
}

static void CrashScanPtrFields(HANDLE h, DWORD obj, DWORD* seen, int* nseen)
{
    if (!obj)
        return;
    unsigned d[16];
    memset(d, 0, sizeof(d));
    __try
    {
        memcpy(d, (const void*)(DWORD_PTR)obj, sizeof(d));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    for (int i = 0; i < 16; ++i)
        CrashTryKey(h, d[i], seen, nseen);
}

static void CrashWriteKnownSite(HANDLE h, DWORD rva)
{
    const char* msg = 0;
    if (rva >= 0x4A8E00 && rva <= 0x4A8E40)
        msg = "NullVtableUi string fallback (rva 4A8E0B, patched 2.98)";
    else if (rva >= 0x1D0F00 && rva <= 0x1D0F3F)
        msg = "FUN_005D0EB0 identity node, virtual [vtable+0x88] (patched 2.99/3.03)";
    else if (rva >= 0x1D1CC0 && rva <= 0x1D1CE0)
        msg = "FUN_005D1BB0 identity [esi+0xE4] cmp (patched 3.04, eax-save 3.05, epilogue-skip 3.06)";
    else if (rva >= 0x1D1BB0 && rva <= 0x1D1BD8)
        msg = "FUN_005D1BB0 prologue (guard 3.08/3.09)";
    else if (rva >= 0x1D4420 && rva <= 0x1D4540)
        msg = "FUN_005D4420 identity [vtable+0x70] (patched 3.09)";
    else if (rva >= 0x113D50 && rva <= 0x113E20)
        msg = "FUN 0x113D50 identity [vtable+0x38] (patched 3.10)";
    else if (rva >= 0x5A63F0 && rva <= 0x5A6418)
        msg = "FUN_005A63F0 string/buffer clear [edi] (patched 3.08)";
    else if (rva >= 0x1B9670 && rva <= 0x1B968C)
        msg = "FUN_005B9670 identity [vtable+0x84] (patched 3.03)";
    else if (rva >= 0x1DF4B0 && rva <= 0x1DF590)
        msg = "FUN_005DF4B0 identity lookup / CMoveCommand+0x38 (guard 3.00)";
    else if (rva >= 0x1AB7F0 && rva <= 0x1AB900)
        msg = "FUN_005AB7F0 identity hash lookup";
    else if (rva >= 0x282EC0 && rva <= 0x283200)
        msg = "FUN_00682EC0 daily MP checksum / OOS dialog";
    if (msg)
        CrashPrintf(h, "  known_site: %s\n", msg);
    else
        CrashPrintf(h, "  known_site: none (new rva %06X — смотреть keys/objects)\n", rva);
}

static void CrashDumpSource(HANDLE h, CONTEXT* ctx)
{
    if (!ctx)
        return;

    CrashWrite(h, "  source:\n");
    DWORD rva = (g_base && ctx->Eip >= g_base && ctx->Eip < g_base + g_imageSize)
        ? (ctx->Eip - g_base) : 0;
    if (rva)
        CrashWriteKnownSite(h, rva);

    DWORD seenObj[16];
    DWORD seenKey[16];
    int nObj = 0;
    int nKey = 0;
    memset(seenObj, 0, sizeof(seenObj));
    memset(seenKey, 0, sizeof(seenKey));

    CrashWrite(h, "  source_objects:\n");
    CrashTryObject(h, "eax", ctx->Eax, seenObj, &nObj, 0);
    CrashTryObject(h, "ecx", ctx->Ecx, seenObj, &nObj, 0);
    CrashTryObject(h, "esi", ctx->Esi, seenObj, &nObj, 0);
    CrashTryObject(h, "edi", ctx->Edi, seenObj, &nObj, 0);
    CrashTryObject(h, "ebx", ctx->Ebx, seenObj, &nObj, 0);
    CrashTryObject(h, "edx", ctx->Edx, seenObj, &nObj, 0);

    if (ctx->Esi)
    {
        DWORD field40 = 0;
        DWORD field44 = 0;
        __try
        {
            field40 = *(DWORD*)(DWORD_PTR)(ctx->Esi + 0x40);
            field44 = *(DWORD*)(DWORD_PTR)(ctx->Esi + 0x44);
            CrashPrintf(h, "    esi+40=%08X esi+44=%08X (часто id/тип identity)\n",
                field40, field44);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    CrashWrite(h, "  source_keys:\n");
    CrashScanPtrFields(h, ctx->Eax, seenKey, &nKey);
    CrashScanPtrFields(h, ctx->Ecx, seenKey, &nKey);
    CrashScanPtrFields(h, ctx->Esi, seenKey, &nKey);
    CrashScanPtrFields(h, ctx->Edi, seenKey, &nKey);

    __try
    {
        for (int i = 0; i < 384; ++i)
        {
            DWORD* slot = (DWORD*)(DWORD_PTR)(ctx->Esp + (DWORD)i * 4);
            if (SafeIsBadReadPtr(slot, 4))
                break;
            DWORD p = *slot;
            CrashTryKey(h, p, seenKey, &nKey);
            CrashTryObject(h, "stack", p, seenObj, &nObj, 1);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashWrite(h, "    (stack scan faulted)\n");
    }

    if (nKey == 0)
        CrashWrite(h, "    (none)\n");
}

// MiniDumpNormal (2.95) = 0 — только стеки.
// Без MiniDumpWithIndirectlyReferencedMemory (0x40): из обработчика
// он долго ходит по куче и может зависнуть — тогда 3.06 не доходил
// до v2dll_crash.log. DataSegs + unloaded + thread + memory-info.
static const DWORD kDumpRich =
    0x00000001 | 0x00000020 | 0x00000800 | 0x00001000;

static LONG g_crashDumpSerial = 0;
static wchar_t g_crashDumpWritten[MAX_PATH];
static wchar_t g_crashLogWritten[MAX_PATH];

static void CrashStampName(wchar_t* dst, size_t cap, const wchar_t* dir,
    const wchar_t* prefix, const wchar_t* ext)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    LONG n = InterlockedIncrement(&g_crashDumpSerial);
    swprintf_s(dst, cap,
        L"%s\\%s_%04u%02u%02u_%02u%02u%02u_%u_%u_%ld%s",
        dir, prefix,
        (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
        (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
        (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(),
        n, ext);
}

static HANDLE CrashCreateFileRetry(const wchar_t* path, DWORD access, DWORD share, DWORD disp)
{
    return CreateFileW(path, access, share, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void CrashPathToUtf8(const wchar_t* src, char* dst, size_t cap)
{
    dst[0] = 0;
    if (!src || !src[0] || cap < 2)
        return;
    int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)cap, NULL, NULL);
    if (n <= 0)
        dst[0] = 0;
}

static void CrashWriteRaw(const wchar_t* path, DWORD disp, const char* text, int len)
{
    HANDLE h = CreateFileW(
        path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    if (disp == OPEN_ALWAYS)
        SetFilePointer(h, 0, NULL, FILE_END);
    DWORD wr = 0;
    if (text && len > 0)
        WriteFile(h, text, (DWORD)len, &wr, NULL);
    FlushFileBuffers(h);
    CloseHandle(h);
}

static bool CrashCodeIsNoise(DWORD code)
{
    return code == 0x40010006 || code == 0x4001000A || code == 0x406D1388 ||
        code == 0x80000003 || code == 0x80000004;
}

static bool CrashCodeIsFatal(DWORD code)
{
    return code == 0xC0000005 || code == 0xC0000006 || code == 0xC000001D ||
        code == 0xC0000094 || code == 0xC00000FD || code == 0xC0000409 ||
        code == 0xC0000374 || code == 0xC0000602 || code == 0xC0000417 ||
        code == 0x40000015;
}

static bool CrashIpInSelf(DWORD addr)
{
    if (!g_selfModule || !addr)
        return false;
    DWORD base = (DWORD)(DWORD_PTR)g_selfModule;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_selfModule;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return addr >= base && addr < base + 0x80000;
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    DWORD size = nt->OptionalHeader.SizeOfImage;
    return addr >= base && addr < base + size;
}

static void CrashBreadcrumb(PEXCEPTION_POINTERS ep, const char* via, bool alsoV2log)
{
    InitLogDir();
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\v2dll_crash_hint.txt", g_logsDir);

    DWORD code = 0, addr = 0, eip = 0;
    if (ep && ep->ExceptionRecord)
    {
        code = ep->ExceptionRecord->ExceptionCode;
        addr = (DWORD)(DWORD_PTR)ep->ExceptionRecord->ExceptionAddress;
    }
    if (ep && ep->ContextRecord)
        eip = ep->ContextRecord->Eip;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[512];
    int n = sprintf_s(buf,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u dll=%s via=%s pid=%u tid=%u "
        "code=%08X eip=%08X addr=%08X\r\n",
        (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
        (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
        (unsigned)st.wMilliseconds,
        MOD_VERSION, via ? via : "-",
        (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId(),
        code, eip, addr);
    if (n > 0)
        CrashWriteRaw(path, CREATE_ALWAYS, buf, n);

    if (alsoV2log && g_logFile[0] && n > 0)
        CrashWriteRaw(g_logFile, OPEN_ALWAYS, buf, n);
}

static HANDLE CrashOpenLog()
{
    InitLogDir();
    wcscpy_s(g_crashLogWritten, g_crashLogFile);
    HANDLE h = CrashCreateFileRetry(
        g_crashLogFile,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_ALWAYS);
    if (h != INVALID_HANDLE_VALUE)
        return h;

    CrashStampName(g_crashLogWritten, MAX_PATH, g_logsDir, L"v2dll_crash", L".log");
    h = CrashCreateFileRetry(
        g_crashLogWritten,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_ALWAYS);
    if (h != INVALID_HANDLE_VALUE)
        return h;

    wchar_t exeDir[MAX_PATH];
    wcscpy_s(exeDir, g_logsDir);
    wchar_t* slash = wcsrchr(exeDir, L'\\');
    if (slash && _wcsicmp(slash, L"\\Logs") == 0)
        *slash = 0;
    CrashStampName(g_crashLogWritten, MAX_PATH, exeDir, L"v2dll_crash", L".log");
    return CrashCreateFileRetry(
        g_crashLogWritten,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_ALWAYS);
}

static DWORD CrashWriteDumpTo(const wchar_t* path, PEXCEPTION_POINTERS ep)
{
    HMODULE dbg = GetModuleHandleW(L"dbghelp.dll");
    if (!dbg)
        dbg = LoadLibraryW(L"dbghelp.dll");
    if (!dbg)
        return 0;

    typedef struct {
        DWORD ThreadId;
        PEXCEPTION_POINTERS ExceptionPointers;
        BOOL ClientPointers;
    } MiniDumpExceptionInfo;

    typedef BOOL(WINAPI* tMiniDumpWriteDump)(
        HANDLE, DWORD, HANDLE, DWORD, MiniDumpExceptionInfo*, void*, void*);

    tMiniDumpWriteDump fn = (tMiniDumpWriteDump)GetProcAddress(dbg, "MiniDumpWriteDump");
    if (!fn)
        return 0;

    HANDLE file = CrashCreateFileRetry(
        path, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS);
    if (file == INVALID_HANDLE_VALUE)
        return 0;

    MiniDumpExceptionInfo info;
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = ep;
    info.ClientPointers = FALSE;

    DWORD used = kDumpRich;
    BOOL ok = FALSE;
    __try
    {
        ok = fn(GetCurrentProcess(), GetCurrentProcessId(), file, used, &info, 0, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = FALSE;
    }

    if (!ok)
    {
        SetFilePointer(file, 0, NULL, FILE_BEGIN);
        SetEndOfFile(file);
        used = 0;
        __try
        {
            ok = fn(GetCurrentProcess(), GetCurrentProcessId(), file, used, &info, 0, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ok = FALSE;
        }
        if (!ok)
            used = 0;
    }

    CloseHandle(file);
    if (!ok)
        DeleteFileW(path);
    return ok ? used : 0;
}

static DWORD CrashWriteDump(PEXCEPTION_POINTERS ep)
{
    g_crashDumpWritten[0] = 0;
    if (!ep || (ep->ExceptionRecord && ep->ExceptionRecord->ExceptionCode == 0xC00000FD))
        return 0;

    InitLogDir();

    wchar_t path[MAX_PATH];
    CrashStampName(path, MAX_PATH, g_logsDir, L"v2dll_crash", L".dmp");
    DWORD used = CrashWriteDumpTo(path, ep);
    if (!used)
    {
        wchar_t exeDir[MAX_PATH];
        wcscpy_s(exeDir, g_logsDir);
        wchar_t* slash = wcsrchr(exeDir, L'\\');
        if (slash && _wcsicmp(slash, L"\\Logs") == 0)
            *slash = 0;
        CrashStampName(path, MAX_PATH, exeDir, L"v2dll_crash", L".dmp");
        used = CrashWriteDumpTo(path, ep);
    }
    if (used)
        wcscpy_s(g_crashDumpWritten, path);
    return used;
}

static void ReportCrash(PEXCEPTION_POINTERS ep)
{
    if (!g_settings.enableCrashLog)
        return;
    if (ep && ep->ExceptionRecord)
    {
        if (CrashCodeIsNoise(ep->ExceptionRecord->ExceptionCode))
            return;
    }
    if (InterlockedCompareExchange(&g_inCrash, 1, 0) != 0)
        return;

    InitLogDir();
    CrashBreadcrumb(ep, "uef", true);

    HANDLE h = CrashOpenLog();
    if (h == INVALID_HANDLE_VALUE)
        return;

    DWORD dumpType = 0;

    __try
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        CrashPrintf(h,
            "\n======== CRASH %04u-%02u-%02u %02u:%02u:%02u.%03u dll=%s pid=%u tid=%u ========\n",
            (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
            (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
            (unsigned)st.wMilliseconds,
            MOD_VERSION, (unsigned)GetCurrentProcessId(), (unsigned)GetCurrentThreadId());

        unsigned int cw = 0;
        _controlfp_s(&cw, 0, 0);
        CrashPrintf(h, "  fpu_cw=%08X mxcsr=%08X\n", cw, _mm_getcsr());
        CrashPrintf(h, "  oos_hits=%d sync_hits=%d last=%s\n",
            g_oosHits, g_syncHits, g_lastChecksumLine);
        {
            char ident[320];
            FormatIdentDiag(ident, sizeof(ident));
            CrashPrintf(h, "  %s\n", ident);
        }

        EXCEPTION_RECORD* rec = ep ? ep->ExceptionRecord : 0;
        CONTEXT* ctx = ep ? ep->ContextRecord : 0;
        DWORD code = rec ? rec->ExceptionCode : 0;
        DWORD addr = rec ? (DWORD)(DWORD_PTR)rec->ExceptionAddress : 0;
        CrashPrintf(h, "  code=%08X (%s)\n", code, CrashCodeName(code));
        CrashLogAddr(h, "fault", addr);

        if (rec && code == 0xC0000005 && rec->NumberParameters >= 2)
        {
            CrashPrintf(h, "  av_%s addr=%08X\n",
                rec->ExceptionInformation[0] ? "write" : "read",
                (DWORD)rec->ExceptionInformation[1]);
        }

        if (ctx)
        {
            CrashPrintf(h,
                "  eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X\n"
                "  ebp=%08X esp=%08X eip=%08X eflags=%08X\n",
                ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi,
                ctx->Ebp, ctx->Esp, ctx->Eip, ctx->EFlags);
            CrashLogAddr(h, "eip", ctx->Eip);
            CrashDumpCode(h, ctx->Eip);

            CrashWrite(h, "  pointed:\n");
            CrashDumpPtr(h, "eax", ctx->Eax);
            CrashDumpNested(h, "eax", ctx->Eax, 0x14);
            CrashDumpNested(h, "eax", ctx->Eax, 0x18);
            CrashDumpPtr(h, "ebx", ctx->Ebx);
            CrashDumpPtr(h, "ecx", ctx->Ecx);
            CrashDumpPtr(h, "edx", ctx->Edx);
            CrashDumpPtr(h, "esi", ctx->Esi);
            CrashDumpPtr(h, "edi", ctx->Edi);
            CrashDumpPtr(h, "esp", ctx->Esp);

            CrashWrite(h, "  stack:\n");
            DWORD ebp = ctx->Ebp;
            CrashLogAddr(h, "  [0]", ctx->Eip);
            for (int i = 1; i <= 24; ++i)
            {
                if (SafeIsBadReadPtr((const void*)(DWORD_PTR)ebp, 8))
                    break;
                DWORD next = *(DWORD*)(DWORD_PTR)ebp;
                DWORD ret = *(DWORD*)(DWORD_PTR)(ebp + 4);
                char tag[16];
                sprintf_s(tag, "  [%d]", i);
                CrashLogAddr(h, tag, ret);
                if (!next || next <= ebp)
                    break;
                ebp = next;
            }

            CrashDumpSource(h, ctx);
        }

        FlushFileBuffers(h);
        dumpType = CrashWriteDump(ep);

        DWORD dumpSize = 0;
        if (g_crashDumpWritten[0])
        {
            HANDLE dumpFile = CreateFileW(
                g_crashDumpWritten, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (dumpFile != INVALID_HANDLE_VALUE)
            {
                dumpSize = GetFileSize(dumpFile, NULL);
                CloseHandle(dumpFile);
            }
        }
        char dumpUtf8[MAX_PATH * 3];
        CrashPathToUtf8(g_crashDumpWritten, dumpUtf8, sizeof(dumpUtf8));
        if (!dumpUtf8[0])
            strcpy_s(dumpUtf8, "(none)");
        CrashPrintf(h, "  dump=%s type=%08X size=%u\n", dumpUtf8, dumpType, dumpSize);
        if (g_crashLogWritten[0] && _wcsicmp(g_crashLogWritten, g_crashLogFile) != 0)
        {
            char logUtf8[MAX_PATH * 3];
            CrashPathToUtf8(g_crashLogWritten, logUtf8, sizeof(logUtf8));
            CrashPrintf(h, "  crash_log_fallback=%s\n", logUtf8[0] ? logUtf8 : "(none)");
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        CrashWrite(h, "  (crash logger itself faulted)\n");
    }

    FlushFileBuffers(h);
    CloseHandle(h);
}

static LONG CALLBACK CrashVectored(EXCEPTION_POINTERS* ep)
{
    if (!ep || !ep->ExceptionRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (CrashCodeIsNoise(code) || !CrashCodeIsFatal(code))
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD addr = (DWORD)(DWORD_PTR)ep->ExceptionRecord->ExceptionAddress;
    if (CrashIpInSelf(addr))
        return EXCEPTION_CONTINUE_SEARCH;
    CrashBreadcrumb(ep, "veh", false);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI CrashUnhandled(EXCEPTION_POINTERS* ep)
{
    ReportCrash(ep);
    if (g_prevUef)
        return g_prevUef(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI HookSetUEF(LPTOP_LEVEL_EXCEPTION_FILTER p)
{
    if (p == CrashUnhandled)
        return g_realSetUEF ? g_realSetUEF(p) : 0;
    g_prevUef = p;
    if (g_realSetUEF)
        return g_realSetUEF(CrashUnhandled);
    return 0;
}

static void CrashFromAbort(DWORD code)
{
    EXCEPTION_RECORD rec;
    CONTEXT ctx;
    EXCEPTION_POINTERS ep;
    memset(&rec, 0, sizeof(rec));
    memset(&ctx, 0, sizeof(ctx));
    RtlCaptureContext(&ctx);
    rec.ExceptionCode = code;
    rec.ExceptionAddress = (void*)(DWORD_PTR)ctx.Eip;
    ep.ExceptionRecord = &rec;
    ep.ContextRecord = &ctx;
    ReportCrash(&ep);
}

static void __cdecl CrashOnAbort(int)
{
    CrashFromAbort(0x40000015);
    TerminateProcess(GetCurrentProcess(), 3);
}

static void __cdecl CrashOnInvalidParam(
    const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    CrashFromAbort(0xC0000417);
    TerminateProcess(GetCurrentProcess(), 3);
}

static void InstallCrashWatch()
{
    InitLogDir();

    ULONG guarantee = 32768;
    SetThreadStackGuarantee(&guarantee);

    AddVectoredExceptionHandler(1, CrashVectored);

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (k32)
        g_realSetUEF = (tSetUnhandledExceptionFilter)GetProcAddress(
            k32, "SetUnhandledExceptionFilter");
    if (g_realSetUEF)
        g_prevUef = g_realSetUEF(CrashUnhandled);
    else
        g_prevUef = SetUnhandledExceptionFilter(CrashUnhandled);

    HMODULE exe = GetModuleHandleA(NULL);
    if (exe && g_realSetUEF)
        HookIat(exe, "kernel32.dll", "SetUnhandledExceptionFilter",
            (void*)HookSetUEF, (void**)&g_realSetUEF);

    signal(SIGABRT, CrashOnAbort);
    _set_invalid_parameter_handler(CrashOnInvalidParam);

    Log("CrashWatch: Logs\\v2dll_crash.log + v2dll_crash_*.dmp + v2dll_crash_hint.txt");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        g_selfModule = hModule;
        InitLogDir();
        InitializeCriticalSection(&g_logCs);
        g_logCsInit = true;
        InstallCrashWatch();
        PinFpu();
        Log("DllMain: attach, Install = %d", (int)Install());
    }
    return TRUE;
}