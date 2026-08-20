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
#include <intrin.h>

#include "lua51_exports.h"

// ---------------------------------------------------------------
// Настройки сборки
// ---------------------------------------------------------------

// Версия. Игра не сверяет бинарники, поэтому единственная защита от
// "у кого-то старая DLL" — сравнить эту строку в логах перед сетевой
// игрой.
#define MOD_VERSION "1.1"

// Лог пишется в v2button.log рядом с exe, очищается при запуске.
// Для раздачи ставить 0: фильтр решений вызывается тысячами раз за тик.
#define ENABLE_LOG 1

#define ENABLE_BUTTONS         1   // кнопки, запускающие решения
#define ENABLE_DECISION_FILTER 1   // скрытие решений из окна политики
#define ENABLE_PRICE_DELTA     1   // процентный шаг изменения цен
#define ENABLE_EXE_PATCHES     1   // подкрепления и цели войны
#define ENABLE_INDICATORS      1   // текстовые индикаторы
#define ENABLE_POP_DISPLAY     1   // общее население в верхней панели


// 0 — переписывать строку внутри объекта элемента. Строка находится,
//     но на экране не меняется: элемент кеширует разложенный текст.
// 1 — показывать значение подсказкой при наведении. Ничего угадывать
//     не нужно: слот подсказки мы и так перехватываем, а элемент под
//     курсором приходит туда аргументом.
#define INDICATOR_MODE 1

static bool g_logStarted = false;

static void Log(const char* fmt, ...)
{
#if ENABLE_LOG
    FILE* f = 0;
    if (fopen_s(&f, "v2button.log", g_logStarted ? "a" : "w") != 0 || !f)
        return;

    g_logStarted = true;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fprintf(f, "\n");
    fclose(f);
#else
    (void)fmt;
#endif
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
    { "CProductionView", 0xA0FECC, 10, true,  0x00, 0x4C, 0                      },
};

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

// Текстовые индикаторы. host: 0 — контейнер вида, 1 — окно постройки.
//
// probe — текст, который элемент показывает до нашего вмешательства.
// Именно по нему находится строка внутри объекта: движок хранит там
// уже разрешённую локализацию, а не ключ. Задайте в localisation/*.csv
// для этого элемента что-нибудь короткое и уникальное и впишите сюда
// то же самое.
struct IndicatorDef
{
    int         view;
    int         host;
    const char* element;
    const char* probe;
};

static const int HOST_VIEW = 0;
static const int HOST_BUILD_FACTORY = 1;

static const IndicatorDef INDICATORS[] =
{
    { 2, HOST_BUILD_FACTORY, "FE_CORRUPTION_MODIFIER_BDSM", "XXXXXXXX" },
};

static const int VIEW_COUNT = sizeof(VIEWS) / sizeof(VIEWS[0]);
static const int BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);
static const int INDICATOR_COUNT = sizeof(INDICATORS) / sizeof(INDICATORS[0]);

// Потолки: под каждый вид и каждую кнопку нужен свой переходник,
// а их приходится объявлять заранее — см. макросы ниже.
static const int MAX_VIEWS = 4;
static const int MAX_BUTTONS = 8;
static const int MAX_INDICATORS = 4;


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

// Страна игрока: массив стран и индекс. Взято из кода самой игры.
static const DWORD RVA_COUNTRY_ARRAY = 0xE587E4;
static const DWORD RVA_GAME_STATE = 0xE588E8;

static const int OFF_COUNTRY_LIST = 0x04;
static const int OFF_PLAYER_INDEX = 0xB60;

// Административная эффективность: int64, 15 бит дробной части.
// Игра показывает её как value * 100 >> 15.
static const int OFF_ADMIN_EFFICIENCY = 0xE70;

// Окно постройки завода. CProductionView хранит его по +0x1C4,
// сам контейнер лежит в окне по +0x28. Подтверждено замером.
static const DWORD RVA_VTABLE_BUILDFACTORY = 0xA0FCF0;
static const int   OFF_VIEW_BF_WINDOW = 0x1C4;
static const int   OFF_BF_CONTAINER = 0x28;


// ---------------------------------------------------------------
// Смещения внутри объектов интерфейса
// ---------------------------------------------------------------

static const int GLUE_SIZE = 44;    // размер склейки
static const int OFF_GLUE_METHOD = 0x08;  // склейка -> указатель на метод

static const int VT_FIND_WINDOW = 0x6C;  // контейнер: найти вложенное окно
static const int VT_FIND_CHILD = 0x34;  // окно: найти кнопку по имени
static const int VT_FIND_TEXT = 0x3C;  // окно: найти текстовое поле
static const int OFF_OBSERVABLE = 0x54;  // кнопка -> Observable
static const int VT_ADD_OBSERVER = 0x04;  // Observable: AddObserver
static const int VT_GET_NAME = 0x44;  // элемент: имя из .gui

// Поддельный элемент для OnMakeDecisionClicked: функция читает у него
// только +0x14 (данные std::string) и +0x28 (_Myres).
static const int ELEM_STRDATA = 0x14;
static const int ELEM_STRRES = 0x28;

// Глубина поиска строки внутри объекта элемента.
static const int ELEMENT_SCAN_BYTES = 0x400;
static const int MAX_TEXT_SLOTS = 6;


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

static unsigned char g_glue[MAX_BUTTONS][GLUE_SIZE];
static unsigned char g_fakeElem[MAX_BUTTONS][0x30];
static char          g_decisionText[MAX_BUTTONS][128];
static char          g_nameStorage[128];
static char          g_indicatorText[64];

// Смещения строк с ключом внутри объекта элемента. После первой
// перезаписи искать по ключу нечего, поэтому запоминаем.
static int g_textOffset[MAX_INDICATORS][MAX_TEXT_SLOTS];
static int g_textCount[MAX_INDICATORS] = { 0 };
static int g_textLogged[MAX_INDICATORS] = { 0 };


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


// ---------------------------------------------------------------
// Страна игрока и текст индикатора
// ---------------------------------------------------------------

static unsigned char* PlayerCountry()
{
    DWORD* pList = *(DWORD**)(g_base + RVA_COUNTRY_ARRAY);
    DWORD* pGame = *(DWORD**)(g_base + RVA_GAME_STATE);

    if (!pList || !pGame)
        return 0;

    DWORD list = *(DWORD*)((unsigned char*)pList + OFF_COUNTRY_LIST);
    DWORD index = *(DWORD*)((unsigned char*)pGame + OFF_PLAYER_INDEX);

    if (!list)
        return 0;

    return *(unsigned char**)(list + index * 4);
}


// 100 минус административная эффективность, с одним знаком.
static const char* BuildIndicatorText(int index)
{
    (void)index;

    g_indicatorText[0] = 0;

    unsigned char* country = PlayerCountry();
    if (!country)
        return g_indicatorText;

    long long eff = *(long long*)(country + OFF_ADMIN_EFFICIENCY);
    long long tenths = (eff * 1000) >> 15;
    long long left = 1000 - tenths;

    if (left < 0)
        left = 0;

    sprintf_s(g_indicatorText, sizeof(g_indicatorText), "%d.%d%%",
        (int)(left / 10), (int)(left % 10));

    return g_indicatorText;
}


// ---------------------------------------------------------------
// Индикаторы
//
// Текст меняем перезаписью строки прямо внутри объекта элемента:
// установка через методы (0x78 у элемента, 0x38 у контейнера) не
// сработала — первая молча ничего не давала, вторая валила игру.
// ---------------------------------------------------------------

static int FindStringsInObject(void* object, const char* text,
    int* offsets, int maxOffsets)
{
    unsigned char* base = (unsigned char*)object;
    unsigned want = (unsigned)strlen(text);
    int found = 0;

    for (int off = 0; off + 0x18 <= ELEMENT_SCAN_BYTES && found < maxOffsets; off += 4)
    {
        unsigned char* p = base + off;

        unsigned size = *(unsigned*)(p + 0x10);
        unsigned res = *(unsigned*)(p + 0x14);

        if (size != want || res < size || res > 0x1000)
            continue;

        const char* data = (res > 15) ? *(const char**)p : (const char*)p;
        if (!data)
            continue;

        if (memcmp(data, text, want) == 0)
            offsets[found++] = off;
    }

    return found;
}


// Длиннее прежней ёмкости не пишем, чтобы не трогать чужую кучу.
static void WriteStringInPlace(void* object, int offset, const char* text)
{
    unsigned char* p = (unsigned char*)object + offset;

    unsigned res = *(unsigned*)(p + 0x14);
    unsigned len = (unsigned)strlen(text);

    if (len > res)
        return;

    char* data = (res > 15) ? *(char**)p : (char*)p;
    if (!data)
        return;

    memcpy(data, text, len + 1);
    *(unsigned*)(p + 0x10) = len;
}


// Контейнер окна постройки завода, взятый через поле вида.
static void* BuildFactoryContainer(void* productionView)
{
    if (!productionView)
        return 0;

    void* window =
        *(void**)((unsigned char*)productionView + OFF_VIEW_BF_WINDOW);

    if (!window)
        return 0;   // окно ещё не открывали

    // Окно уничтожается при закрытии — проверяем, что это всё ещё оно.
    if (*(DWORD*)window != g_base + RVA_VTABLE_BUILDFACTORY)
        return 0;

    return *(void**)((unsigned char*)window + OFF_BF_CONTAINER);
}


static void UpdateIndicators(int viewIndex, void* view)
{
    const ViewDef& vd = VIEWS[viewIndex];

    void* container = *(void**)((unsigned char*)view + vd.offContainer);
    if (!container)
        return;

    for (int i = 0; i < INDICATOR_COUNT && i < MAX_INDICATORS; ++i)
    {
        if (INDICATORS[i].view != viewIndex)
            continue;

        void* host = container;

        if (INDICATORS[i].host == HOST_BUILD_FACTORY)
        {
            host = BuildFactoryContainer(view);
            if (!host)
                continue;   // окно не открыто — это нормально
        }
        else if (vd.window)
        {
            GStr sWindow;
            MakeStr(&sWindow, g_nameStorage, sizeof(g_nameStorage), vd.window);

            host = VCall1(container, VT_FIND_WINDOW, &sWindow);
            if (!host)
                continue;
        }

        GStr sName;
        MakeStr(&sName, g_nameStorage, sizeof(g_nameStorage), INDICATORS[i].element);

        void* element = VCall1(host, VT_FIND_TEXT, &sName);
        if (!element)
            continue;

        // Ключ может лежать в объекте не один раз: исходник и
        // разрешённая копия. Ищем все вхождения один раз.
        if (g_textCount[i] == 0)
        {
            g_textCount[i] = FindStringsInObject(
                element, INDICATORS[i].probe,
                g_textOffset[i], MAX_TEXT_SLOTS);

            if (!g_textLogged[i])
            {
                g_textLogged[i] = 1;

                char list[128] = { 0 };
                for (int k = 0; k < g_textCount[i]; ++k)
                {
                    char one[16];
                    sprintf_s(one, sizeof(one), "%X ", g_textOffset[i][k]);
                    strcat_s(list, sizeof(list), one);
                }

                Log("Indicator '%s': element=%08X, ищем '%s', вхождений %d, смещения %s",
                    INDICATORS[i].element, (DWORD)(DWORD_PTR)element,
                    INDICATORS[i].probe, g_textCount[i], list);
            }
        }

        const char* text = BuildIndicatorText(i);

        for (int k = 0; k < g_textCount[i]; ++k)
            WriteStringInPlace(element, g_textOffset[i][k], text);
    }
}


// Подсказка над элементом. Вызывается после оригинала: тот кладёт
// свой текст в retBuf, а мы подменяем его, если курсор над нашим
// индикатором.
static void OnTooltip(int viewIndex, void* retBuf, void* element)
{
    static int logged = 0;

    if (!retBuf || !element)
        return;

    const char* name = GStrText(VCall0(element, VT_GET_NAME));


    for (int i = 0; i < INDICATOR_COUNT && i < MAX_INDICATORS; ++i)
    {
        if (INDICATORS[i].view != viewIndex)
            continue;

        if (strcmp(name, INDICATORS[i].element) != 0)
            continue;

        const char* text = BuildIndicatorText(i);
        GStrSet(retBuf, text);

        if (logged < 3)
        {
            ++logged;
            Log("Tooltip '%s': подставлено '%s'", name, text);
        }
    }
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

#if ENABLE_INDICATORS && INDICATOR_MODE == 0
    // Каждый раз: значение меняется по ходу игры.
    UpdateIndicators(viewIndex, view);
#endif

    // Указатель сменился — вид пересоздан, например новой партией.
    if (view == g_configuredView[viewIndex])
        return;

#if ENABLE_BUTTONS
    if (SetupButtons(viewIndex, view))
        g_configuredView[viewIndex] = view;
#endif
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
};

static const int EXE_PATCH_COUNT = sizeof(EXE_PATCHES) / sizeof(EXE_PATCHES[0]);


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


static bool Install()
{
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

#if ENABLE_DECISION_FILTER
    {
        bool ok = PatchSlot(RVA_VTABLE_DECISION, VT_SLOT_ISVALID,
            (void*)&MyDecisionIsValid, (void**)&g_origIsValid);
        Log("patch CDecision: слот %d = %d", VT_SLOT_ISVALID, (int)ok);
    }
#endif

#if ENABLE_EXE_PATCHES
    InstallExePatches();
#endif

#if ENABLE_PRICE_DELTA
    InstallPriceDelta();
#endif

#if ENABLE_POP_DISPLAY
    InstallPopDisplay();
#endif

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