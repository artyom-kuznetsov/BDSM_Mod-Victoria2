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
#include <intrin.h>
#include <ctype.h>

#include "lua51_exports.h"

// ---------------------------------------------------------------
// Настройки сборки
// ---------------------------------------------------------------

// Версия. Игра не сверяет бинарники, поэтому единственная защита от
// "у кого-то старая DLL" — сравнить эту строку в логах перед сетевой
// игрой.
// CLAUDE МЕНЯЙ ВЕРСИЮ ПРИ КАЖДОЙ ПРАВКЕ ФАЙЛА
#define MOD_VERSION "2.69"

// Настройки ниже читаются из v2dll_settings.ini рядом с exe при
// каждом запуске игры. Если файла ещё нет, он создаётся со
// значениями по умолчанию (перечисленными здесь). Правка файла не
// требует пересборки DLL - изменения применяются при следующем
// запуске игры.
struct Settings
{
    bool log            = true;   // лог в v2dll.log (много записей на тик, для раздачи ставить 0)
    bool buttons        = true;   // кнопки, запускающие решения
    bool decisionFilter = true;   // скрытие решений из окна политики
    bool priceDelta     = false;  // процентный шаг изменения цен
    bool popDisplay     = false;  // общее население в верхней панели (откачено: не смогли дописать сырое число в скобках без риска)
    bool versionLabel   = true;   // версия мода в подписи главного меню

    // Байтовые патчи exe из таблицы EXE_PATCHES (см. ниже по файлу)
    // управляются напрямую через BytePatch::enabled по ключам
    // PATCH_<ИМЯ> в ini - здесь только патчи exe, не входящие в эту
    // таблицу (каждый - отдельная функция со своим хуком).
    bool patchOccupiedReinforceSplit = true;
    bool patchAllyOwnerCheck         = true;
    bool patchCivilizeNullCheck      = true;
    bool patchGraphPointClamp        = true;
    bool patchFactoryDumpScan        = true;
    bool patchProdListVisibility     = true;
    bool patchProdTypeGate           = true;

    // Разброс броска в бою. Работает только если exe уже несёт
    // "пещеру" от стороннего Vic2_Roll_Changer.py (см. комментарий
    // у InstallCombatRoll ниже по файлу) - без неё сигнатура не
    // совпадёт и патч тихо пропустится.
    bool patchCombatRoll = true;
    int  combatRollMin   = 0;   // минимум броска
    int  combatRollMax   = 4;   // максимум броска
};

static Settings g_settings;

// Загрузка/сохранение настроек (v2dll_settings.ini) реализовано
// ниже по файлу, после таблицы EXE_PATCHES - подстановка значений
// по ключам PATCH_<ИМЯ> ищет патч в этой таблице по имени.
static void LoadSettings();

static bool g_logStarted = false;

static void Log(const char* fmt, ...)
{
    if (!g_settings.log)
        return;

    FILE* f = 0;
    if (fopen_s(&f, "v2dll.log", g_logStarted ? "a" : "w") != 0 || !f)
        return;

    g_logStarted = true;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fprintf(f, "\n");
    fclose(f);
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
static void* g_fnOnMakeDecision = 0;

typedef BOOL(WINAPI* tIsBadReadPtr)(const void*, UINT_PTR);
static tIsBadReadPtr g_fnIsBadReadPtr = 0;

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
// только по имени. Сейчас это fishery (is_coastal = yes).
static const char* const PRODTYPE_EXTRA_WHITELIST[] = { "fishery" };
static const int PRODTYPE_EXTRA_WHITELIST_COUNT =
    sizeof(PRODTYPE_EXTRA_WHITELIST) / sizeof(PRODTYPE_EXTRA_WHITELIST[0]);

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

    for (int e = 0; e < PRODTYPE_EXTRA_WHITELIST_COUNT; ++e)
        if (strcmp(PRODTYPE_EXTRA_WHITELIST[e], name) == 0)
            return 1;

    for (int t = 0; t < g_productionTypeCount; ++t)
    {
        if (strcmp(g_productionTypeNames[t], name) == 0)
            return g_limitByLocalSupply[t] ? 1 : 0;
    }
    return 0;
}

static DWORD g_prodTypeGateWhitelisted = 0;

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

    g_fnIsBadReadPtr = (tIsBadReadPtr)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsBadReadPtr");
    if (!g_fnIsBadReadPtr)
        Log("ProdTypeGateHook: IsBadReadPtr не найден - защитная проверка указателя отключена");

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
    unsigned char expect[8];
    unsigned char replace[8];
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
    if (_stricmp(key, "PATCH_GRAPH_POINT_CLAMP") == 0)         { g_settings.patchGraphPointClamp        = v; return; }
    if (_stricmp(key, "PATCH_FACTORY_DUMP_SCAN") == 0)         { g_settings.patchFactoryDumpScan        = v; return; }
    if (_stricmp(key, "PATCH_PROD_LIST_VISIBILITY") == 0)      { g_settings.patchProdListVisibility     = v; return; }
    if (_stricmp(key, "PATCH_PROD_TYPE_GATE") == 0)            { g_settings.patchProdTypeGate           = v; return; }
    if (_stricmp(key, "PATCH_COMBAT_ROLL") == 0)                { g_settings.patchCombatRoll             = v; return; }

    if (_stricmp(key, "COMBAT_ROLL_MIN") == 0) { g_settings.combatRollMin = atoi(value); return; }
    if (_stricmp(key, "COMBAT_ROLL_MAX") == 0) { g_settings.combatRollMax = atoi(value); return; }

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

static void WriteDefaultSettings(const char* path)
{
    FILE* f = 0;
    if (fopen_s(&f, path, "w") != 0 || !f)
        return;

    fprintf(f,
        "; Настройки V2DLL. 1 = включено, 0 = выключено.\n"
        "; Правится вручную, без пересборки DLL - изменения\n"
        "; применяются при следующем запуске игры.\n"
        "\n"
        "ENABLE_LOG=%d                 ; лог в v2dll.log (много записей на тик, для раздачи ставить 0)\n"
        "ENABLE_BUTTONS=%d             ; кнопки, запускающие решения\n"
        "ENABLE_DECISION_FILTER=%d     ; скрытие решений из окна политики\n"
        "ENABLE_PRICE_DELTA=%d         ; процентный шаг изменения цен\n"
        "ENABLE_POP_DISPLAY=%d         ; общее население в верхней панели (экспериментально)\n"
        "ENABLE_VERSION_LABEL=%d       ; версия мода в подписи главного меню\n",
        (int)g_settings.log, (int)g_settings.buttons, (int)g_settings.decisionFilter,
        (int)g_settings.priceDelta, (int)g_settings.popDisplay, (int)g_settings.versionLabel);

    fprintf(f,
        "\n"
        "; --- Байтовые правки exe (список = таблица EXE_PATCHES в\n"
        "; V2TechButton.cpp, там же подробное описание каждого патча) ---\n");

    for (int i = 0; i < EXE_PATCH_COUNT; ++i)
    {
        char nameUpper[64];
        size_t nlen = strlen(EXE_PATCHES[i].name);
        if (nlen >= sizeof(nameUpper))
            nlen = sizeof(nameUpper) - 1;
        size_t j = 0;
        for (; j < nlen; ++j)
            nameUpper[j] = (char)toupper((unsigned char)EXE_PATCHES[i].name[j]);
        nameUpper[j] = '\0';

        fprintf(f, "PATCH_%s=%d\n", nameUpper, (int)EXE_PATCHES[i].enabled);
    }

    fprintf(f,
        "\n"
        "; --- Отдельные хуки exe (не из таблицы выше) ---\n"
        "PATCH_OCCUPIED_REINFORCE_SPLIT=%d   ; своя ставка пополнения occupied vs allied (уточняет ALLIED_REINFORCE_150)\n"
        "PATCH_ALLY_OWNER_CHECK=%d           ; своя ставка occupied-by-ally vs owned-by-ally (уточняет OCCUPIED_REINFORCE_SPLIT)\n"
        "PATCH_GRAPH_POINT_CLAMP=%d          ; кламп точек графика бюджета - чинит краш переполнения буфера, выключать не рекомендуется\n"
        "PATCH_FACTORY_DUMP_SCAN=%d          ; фоновый поток, дампящий в лог память отслеживаемых фабрик (диагностика, на геймплей не влияет)\n"
        "PATCH_PROD_LIST_VISIBILITY=%d       ; видимость строк списка \"Фабрики\" (нужен кнопке \"скрыть колонии\")\n"
        "PATCH_PROD_TYPE_GATE=%d             ; гейт по production_types.txt для PATCH_LOCAL_SUPPLY_FACTORY_IGNORE_COLONIAL\n"
        "\n"
        "; ВАЖНО: если включён любой из PATCH_BUILD_FACTORY_IGNORE_UNCIVILIZED_*\n"
        "; выше, держите PATCH_CIVILIZE_NULL_CHECK тоже включённым - это патч,\n"
        "; который чинит краш игры при цивилизации страны (0xc0000005), а не\n"
        "; независимая настройка. Без него краш вернётся, как только\n"
        "; нецивилизованная страна с построенной фабрикой цивилизуется.\n"
        "PATCH_CIVILIZE_NULL_CHECK=%d\n",
        (int)g_settings.patchOccupiedReinforceSplit,
        (int)g_settings.patchAllyOwnerCheck,
        (int)g_settings.patchGraphPointClamp,
        (int)g_settings.patchFactoryDumpScan,
        (int)g_settings.patchProdListVisibility,
        (int)g_settings.patchProdTypeGate,
        (int)g_settings.patchCivilizeNullCheck);

    fprintf(f,
        "\n"
        "; --- Разброс броска в бою ---\n"
        "; Работает, только если exe уже несёт \"пещеру\" от стороннего\n"
        "; Vic2_Roll_Changer.py (это ваш случай - вы уже запускали его\n"
        "; раньше с 2-5, наш патч просто перезаписывает эти же байты\n"
        "; на лету при каждом запуске игры). Если exe никогда не был\n"
        "; пропатчен этим скриптом, сигнатура не совпадёт и PATCH_COMBAT_ROLL\n"
        "; тихо пропустится - см. лог.\n"
        "; COMBAT_ROLL_MIN должен быть 0..127, COMBAT_ROLL_MAX >= MIN.\n"
        "PATCH_COMBAT_ROLL=%d\n"
        "COMBAT_ROLL_MIN=%d\n"
        "COMBAT_ROLL_MAX=%d\n",
        (int)g_settings.patchCombatRoll,
        g_settings.combatRollMin,
        g_settings.combatRollMax);

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


static bool Install()
{
    LoadSettings();

    g_base = (DWORD)GetModuleHandleA(NULL);
    if (!g_base)
        return false;

    g_fnOnMakeDecision = (void*)(g_base + RVA_ONMAKEDECISION);

    Log("---- Install ---- версия %s", MOD_VERSION);
    Log("base = %08X", g_base);

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

    if (g_settings.priceDelta)
        InstallPriceDelta();

    if (g_settings.popDisplay)
        InstallPopDisplay();

    if (g_settings.versionLabel)
        InstallVersionLabel();

    if (g_settings.patchCombatRoll)
        InstallCombatRoll();

    Log("Install: done");
    return true;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        Log("DllMain: attach, Install = %d", (int)Install());
    }
    return TRUE;
}