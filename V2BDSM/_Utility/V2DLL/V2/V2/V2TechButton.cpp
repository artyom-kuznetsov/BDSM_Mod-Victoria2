// V2TechButton.cpp
//
// Привязывает кнопки мода к решениям из decisions/*.txt и прячет эти
// решения из списка в окне политики.
//
// Собирать: Visual Studio, проект Dynamic-Link Library, платформа x86 (Win32).
//
// Как устроено:
//   1. При загрузке DLL переписываем слот Update в vtable каждого вида
//      из таблицы VIEWS.
//   2. Наш Update получает указатель на вид. Если он новый (новая
//      партия) — подписываем кнопки этого вида и запоминаем указатель.
//   3. Подписка: клонируем склейку из вида, подменяем в клоне указатель
//      на метод, находим кнопку по имени, вешаем клон на её Observable.
//   4. Клик приходит в свой для каждой кнопки переходник, тот зовёт
//      OnMakeDecisionClicked с поддельным элементом, внутри которого
//      лежит имя решения.
//   5. Отдельно переписываем слот 6 в vtable CDecision, чтобы решения
//      мода не показывались в списке окна политики.

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
// Диагностика
//
// Пишет v2button.log рядом с exe игры. Файл очищается при каждом
// запуске. Метод фильтра движок зовёт тысячами вызовов за тик, в том
// числе из потока загрузки, поэтому оттуда логируются только первые
// несколько совпадений. Для готового мода ставьте 0.
// ---------------------------------------------------------------

// Версия сборки. Игра не сверяет бинарники, поэтому единственная
// защита от "у кого-то старая DLL" — сравнить эту строку в логах
// у всех участников перед сетевой игрой.
#define MOD_VERSION "1.0"

#define ENABLE_LOG 1

// Переключатели для поиска виновника при неполадках.
#define ENABLE_BUTTONS         1   // подписка кнопок на решения
#define ENABLE_DECISION_FILTER 1   // скрытие решений из окна политики
#define ENABLE_PRICE_DELTA     1   // процентный шаг изменения цен
#define ENABLE_EXE_PATCHES     1   // байтовые правки: подкрепления, цели войны
#define ENABLE_INDICATORS      1   // текстовые индикаторы

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
// НАСТРОЙКА — правится здесь
// ---------------------------------------------------------------

// Вид — экран игры со своим классом. Чтобы подключить новый, нужны
// четыре числа из Ghidra:
//   rvaVtable   — адрес vftable класса минус 0x400000
//   slotUpdate  — номер слота Update (предпоследний в таблице)
//   offGlue     — смещение любой CButtonObserverGlue внутри вида,
//                 видно в конструкторе по записи vftable склейки
//   offContainer— смещение GUI-контейнера, у всех виденных видов 0x4C
// window — имя вложенного окна из .gui, либо 0, если кнопка лежит
//          прямо в корневом окне вида.
struct ViewDef
{
    const char* name;
    DWORD       rvaVtable;
    int         slot;         // какой слот перехватывать
    bool        tooltipSlot;  // true — слот подсказки, см. ниже
    int         offGlue;
    int         offContainer;
    const char* window;
};

// Перехватываемый слот должен вызываться регулярно, пока окно живо.
//
// У технологий это слот 11 (Update) — проверено, работает. У бюджета
// слот 11 не вызывается ни разу, поэтому берём слот 10: у обоих видов
// там подсказка, а её движок дёргает при каждом наведении мыши.
// Сигнатуры у слотов разные, отсюда флаг tooltipSlot.
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

// Текстовые индикаторы: элемент из .gui, куда мы пишем строку.
// Кнопкой не является, склейка и подписка не нужны — только поиск
// элемента и установка текста, зато на каждом обновлении вида.
// host: 0 — контейнер самого вида, 1 — окно постройки завода.
struct IndicatorDef
{
    int         view;      // на чьих обновлениях перерисовывать
    int         host;
    const char* element;
};

static const int HOST_VIEW = 0;
static const int HOST_BUILD_FACTORY = 1;

static const IndicatorDef INDICATORS[] =
{
    // build_factory — самостоятельное окно, а не часть CProductionView:
    // у его класса всего два слота в vtable, перехватывать нечего.
    // Поэтому ищем через менеджер интерфейса по имени, а обновляемся
    // на подсказках окна производства, откуда оно и открывается.
    // build_factory — отдельное окно со своим классом. Указатель на
    // него ловим в слоте 0 его vtable, обновляемся на подсказках
    // окна производства, откуда оно и открывается.
    { 2, HOST_BUILD_FACTORY, "FE_CORRUPTION_MODIFIER_BDSM" },

};

static const int INDICATOR_COUNT = sizeof(INDICATORS) / sizeof(INDICATORS[0]);

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

// Границы PoliticsView_OnClick — функции, строящей список решений
// в окне политики. Единственное место, где решение надо прятать.
// Глобалы для доступа к стране игрока. Взяты из кода самой игры:
// country = массив_стран[индекс_игрока], ровно так это делает
// окно политики и подсказка окна технологий.
// Окно постройки завода. Слот 0 его таблицы пересоздаёт окно и в
// конце пишет живой контейнер в this + 0x28 — там мы и забираем
// указатель. Слот 1 — деструктор, его не трогаем.
static const DWORD RVA_VTABLE_BUILDFACTORY = 0xA0FCF0;
static const int   BF_SLOT_REFRESH = 0;
static const int   OFF_BF_CONTAINER = 0x28;

// Конструктор окна постройки. Ловим указатель прямо здесь: поле в
// чужом объекте по +0x1C4 нам недоступно, а слот 0 при открытии не
// вызывается. Первые пять байт — push ebp / mov ebp,esp / push -1,
// ровно под пятибайтовый переход.
static const DWORD RVA_BF_CTOR = 0x2F95B0;
static const DWORD RVA_BF_CTOR_RESUME = 0x2F95B5;

static const unsigned char BF_CTOR_SIG[5] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };

// CProductionView хранит созданное окно постройки по этому смещению.
// Подтверждено замером: в CreateBuildFactory регистр ESI совпал с
// указателем на вид, а туда пишется результат конструктора.
static const int OFF_VIEW_BF_WINDOW = 0x1C4;

static const DWORD RVA_COUNTRY_ARRAY = 0xE587E4;  // указатель, данные на +4
static const DWORD RVA_GAME_STATE = 0xE588E8;  // указатель, индекс на +0xB60

static const int OFF_COUNTRY_LIST = 0x04;
static const int OFF_PLAYER_INDEX = 0xB60;

// Административная эффективность: int64 с 15 битами дробной части.
// Игра показывает её как value * 100 >> 15.
static const int OFF_ADMIN_EFFICIENCY = 0xE70;

static const DWORD RVA_POLITICS_DRAW_BEGIN = 0x2DB2E0;
static const DWORD RVA_POLITICS_DRAW_END = 0x2DC750;


// ---------------------------------------------------------------
// Смещения внутри объектов
// ---------------------------------------------------------------

static const int GLUE_SIZE = 44;    // размер склейки, 0x0B дворда
static const int OFF_GLUE_METHOD = 0x08;  // склейка -> указатель на метод

static const int VT_FIND_WINDOW = 0x6C;  // контейнер: найти вложенное окно
static const int VT_FIND_CHILD = 0x34;  // окно: найти кнопку по имени
static const int VT_FIND_TEXT = 0x3C;  // окно: найти текстовое поле
static const int VT_SET_TEXT = 0x78;  // элемент: задать строку

// Контейнер умеет ставить текст сам: SetText(имя элемента, строка).
// Подсмотрено в обновлении окна выбранной технологии, где так
// заполняются year_label и соседние поля.
static const int VT_SET_TEXT_BY_NAME = 0x38;

// 0 — писать в найденный элемент через 0x78 (не сработало),
// 1 — просить контейнер по имени через 0x38.
#define SET_TEXT_MODE 0
static const int OFF_OBSERVABLE = 0x54;  // кнопка -> Observable
static const int VT_ADD_OBSERVER = 0x04;  // Observable: AddObserver

// Имя решения внутри CDecision: std::string по смещению +0x08,
// значит _Myres лежит на +0x1C. Больше 15 — данные в куче.
static const int OFF_DECISION_NAME = 0x08;

// Поддельный элемент для OnMakeDecisionClicked. Функция читает у него
// только два поля: +0x14 — данные std::string, +0x28 — _Myres.
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
// Вызов виртуального метода с соглашением __thiscall.
// MSVC не даёт объявить __thiscall-указатель напрямую, поэтому
// используем __fastcall с фиктивным EDX — раскладка совпадает.
// ---------------------------------------------------------------

typedef void* (__fastcall* tCallOneArg)(void* ecx, void* edx, void* arg);
typedef void* (__fastcall* tCallTwoArgs)(void* ecx, void* edx, void* a1, void* a2);

static void* VCall1(void* obj, int byteSlot, void* arg)
{
    void** vt = *(void***)obj;
    tCallOneArg fn = (tCallOneArg)vt[byteSlot / 4];
    return fn(obj, 0, arg);
}

static void* VCall2(void* obj, int byteSlot, void* a1, void* a2)
{
    void** vt = *(void***)obj;
    tCallTwoArgs fn = (tCallTwoArgs)vt[byteSlot / 4];
    return fn(obj, 0, a1, a2);
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

    Log("FireDecision: returned");
}


// Склейка хранит один указатель на метод, поэтому у каждой кнопки
// должен быть свой обработчик. Различаются они только номером.
//
// Форма с __asm перед каждой инструкцией: блочная запись __asm { ... }
// внутри макроса схлопывается в одну строку и не разбирается.
#define THUNK(n)                                  \
    __declspec(naked) static void Thunk##n()      \
    {                                             \
        __asm push ebp                            \
        __asm mov  ebp, esp                       \
        __asm pushad                              \
        __asm push n                              \
        __asm call FireDecision                   \
        __asm add  esp, 4                         \
        __asm popad                               \
        __asm mov  esp, ebp                       \
        __asm pop  ebp                            \
        __asm ret                                 \
    }

THUNK(0) THUNK(1) THUNK(2) THUNK(3)
THUNK(4) THUNK(5) THUNK(6) THUNK(7)

static void* const THUNKS[MAX_BUTTONS] =
{
    (void*)&Thunk0, (void*)&Thunk1, (void*)&Thunk2, (void*)&Thunk3,
    (void*)&Thunk4, (void*)&Thunk5, (void*)&Thunk6, (void*)&Thunk7,
};


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

    // Кнопка может лежать во вложенном окне, а может прямо в корневом.
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

        Log("Setup[%s]: '%s' подписана, button=%08X",
            vd.name, BUTTONS[i].button, (DWORD)(DWORD_PTR)button);
        ++done;
    }

    return done > 0;
}


// ---------------------------------------------------------------
// Подменённый Update, по одному на вид
// ---------------------------------------------------------------

// Слот Update: __thiscall без стековых аргументов, плоский ret.
typedef void(__fastcall* tUpdate)(void* ecx, void* edx);

// Слот подсказки: __thiscall с двумя стековыми аргументами и ret 8 —
// буфер под результат и элемент, над которым висит мышь. Разобрано
// на TechnologyView_GetTooltip.
typedef void* (__fastcall* tTooltip)(void* ecx, void* edx, void* retBuf, void* element);

static void* g_origSlot[MAX_VIEWS] = { 0 };
static void* g_configuredView[MAX_VIEWS] = { 0 };


// ---------------------------------------------------------------
// Текстовые индикаторы
//
// В отличие от кнопок обновляются при каждом вызове Update: значение
// меняется по ходу игры. Поиск идёт через другой слот контейнера
// (0x3C вместо 0x34), а текст ставится методом 0x78 самого элемента —
// эта пара разобрана на построении списка решений в окне политики.
//
// На мультиплеер не влияет: только чтение и вывод, симуляция не
// затрагивается. У кого DLL нет, тот просто не увидит цифру.
// ---------------------------------------------------------------

static char g_indicatorText[64];
static char g_textStorage[128];   // отдельный буфер под GStr строки

// Указатель на окно постройки завода. Живёт только пока окно открыто,
// поэтому перед каждым использованием сверяем vtable объекта.
static void* g_bfWindow = 0;

typedef int(__fastcall* tBfRefresh)(void* ecx, void* edx);
static tBfRefresh g_origBfRefresh = 0;

static void ApplyIndicators(void* host, int hostKind);

static int __fastcall MyBfRefresh(void* window, void* edx)
{
    // Сначала оригинал: контейнер создаётся именно в нём.
    int result = g_origBfRefresh ? g_origBfRefresh(window, 0) : 1;

    g_bfWindow = window;

    // Ставим значение сразу здесь: окно только что построено, и это
    // единственный момент, когда процент точно актуален.
    return result;
}

// Контейнер окна постройки, взятый через поле вида производства.
// Вызывается из пещеры на входе в конструктор окна.
static void __cdecl StoreBfWindow(void* window)
{
    g_bfWindow = window;
}


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


// Пятибайтовый переход на вход конструктора: восстанавливаем то, что
// перекрыли, запоминаем первый аргумент и возвращаемся.
static bool InstallBfCtorHook()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_BF_CTOR);

    if (memcmp(hook, BF_CTOR_SIG, sizeof(BF_CTOR_SIG)) != 0)
    {
        Log("BfCtor: сигнатура не совпала (%02X %02X %02X %02X %02X)",
            hook[0], hook[1], hook[2], hook[3], hook[4]);
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
        return false;

    int n = 0;

    cave[n++] = 0x55;                                   // push ebp
    cave[n++] = 0x8B; cave[n++] = 0xEC;                 // mov ebp, esp
    cave[n++] = 0x6A; cave[n++] = 0xFF;                 // push -1

    cave[n++] = 0x60;                                   // pushad
    cave[n++] = 0xFF; cave[n++] = 0x75; cave[n++] = 0x08;  // push [ebp+8]
    cave[n++] = 0xB8;                                   // mov eax, StoreBfWindow
    *(DWORD*)(cave + n) = (DWORD)(DWORD_PTR)&StoreBfWindow; n += 4;
    cave[n++] = 0xFF; cave[n++] = 0xD0;                 // call eax
    cave[n++] = 0x83; cave[n++] = 0xC4; cave[n++] = 0x04;  // add esp, 4
    cave[n++] = 0x61;                                   // popad

    cave[n++] = 0xE9;                                   // jmp resume
    *(DWORD*)(cave + n) = (g_base + RVA_BF_CTOR_RESUME) - (DWORD)(cave + n + 4);
    n += 4;

    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)cave - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("BfCtor: перехват встал, пещера %08X, %d байт",
        (DWORD)(DWORD_PTR)cave, n);
    return true;
}

// Страна игрока. Пусто, если партия ещё не загружена.
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


// 100 минус административная эффективность, в процентах с одним
// знаком после запятой.
static const char* BuildIndicatorText(int indicatorIndex)
{
    (void)indicatorIndex;

    g_indicatorText[0] = 0;

    unsigned char* country = PlayerCountry();
    if (!country)
        return g_indicatorText;

    // Фиксированная точка, 15 бит дробной части.
    long long eff = *(long long*)(country + OFF_ADMIN_EFFICIENCY);

    // В десятых долях процента: eff * 1000 >> 15.
    long long tenths = (eff * 1000) >> 15;
    long long left = 1000 - tenths;

    if (left < 0)
        left = 0;

    sprintf_s(g_indicatorText, sizeof(g_indicatorText), "%d.%d%%",
        (int)(left / 10), (int)(left % 10));

    return g_indicatorText;
}


// Пишет текст во все индикаторы, чей host совпал.
static void ApplyIndicators(void* host, int hostKind)
{
    static int logged = 0;

    for (int i = 0; i < INDICATOR_COUNT; ++i)
    {
        if (INDICATORS[i].host != hostKind)
            continue;

        GStr sName;
        MakeStr(&sName, g_nameStorage, sizeof(g_nameStorage), INDICATORS[i].element);

        void* element = VCall1(host, VT_FIND_TEXT, &sName);

        if (logged < 8)
        {
            ++logged;

            unsigned char* c = PlayerCountry();
            long long raw = c ? *(long long*)(c + OFF_ADMIN_EFFICIENCY) : -1;

            Log("Indicator '%s': element=%08X text='%s' eff=%lld",
                INDICATORS[i].element, (DWORD)(DWORD_PTR)element,
                BuildIndicatorText(i), raw);
        }

        GStr sText;
        MakeStr(&sText, g_textStorage, sizeof(g_textStorage),
            BuildIndicatorText(i));

#if SET_TEXT_MODE == 1
        VCall2(host, VT_SET_TEXT_BY_NAME, &sName, &sText);
#else
        if (element)
            VCall1(element, VT_SET_TEXT, &sText);
#endif
    }
}


static void UpdateIndicators(int viewIndex, void* view)
{
    static int logged = 0;

    const ViewDef& vd = VIEWS[viewIndex];
    unsigned char* v = (unsigned char*)view;

    void* container = *(void**)(v + vd.offContainer);
    if (!container)
        return;

    for (int i = 0; i < INDICATOR_COUNT; ++i)
    {
        if (INDICATORS[i].view != viewIndex)
            continue;

        void* host = container;

        if (INDICATORS[i].host == HOST_BUILD_FACTORY)
        {
            host = BuildFactoryContainer(view);

            // Пока окно не открыто — это нормальное состояние, молчим.
            if (!host)
                continue;
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

        if (logged < 6)
        {
            ++logged;

            unsigned char* c = PlayerCountry();
            long long raw = c ? *(long long*)(c + OFF_ADMIN_EFFICIENCY) : -1;

            Log("Indicator '%s': element=%08X country=%08X eff=%lld (%lld.%lld%%)",
                INDICATORS[i].element, (DWORD)(DWORD_PTR)element,
                (DWORD)(DWORD_PTR)c, raw,
                (raw * 1000 >> 15) / 10, (raw * 1000 >> 15) % 10);
        }

        GStr sText;
        MakeStr(&sText, g_textStorage, sizeof(g_textStorage),
            BuildIndicatorText(i));

#if SET_TEXT_MODE == 1
        VCall2(host, VT_SET_TEXT_BY_NAME, &sName, &sText);
#else
        if (element)
            VCall1(element, VT_SET_TEXT, &sText);
#endif
    }
}


static int g_updateSeen[MAX_VIEWS] = { 0 };

static void OnViewUpdate(int viewIndex, void* view)
{
    // Первые вызовы протоколируем всегда: иначе "слот не зовётся"
    // и "подписка не удалась" в логе выглядят одинаково — пусто.
    if (g_updateSeen[viewIndex] < 3)
    {
        ++g_updateSeen[viewIndex];
        Log("Update[%s]: вызван, view=%08X vt=%08X",
            VIEWS[viewIndex].name,
            (DWORD)(DWORD_PTR)view,
            view ? *(DWORD*)view : 0);
    }

    if (!view)
        return;

#if ENABLE_INDICATORS
    // Каждый раз: значение меняется по ходу игры.
    UpdateIndicators(viewIndex, view);
#endif

    if (view == g_configuredView[viewIndex])
        return;

    // Указатель сменился — вид пересоздан, например новой партией.
    if (SetupButtons(viewIndex, view))
        g_configuredView[viewIndex] = view;
}

#define VIEW_THUNKS(n)                                                     \
    static void __fastcall Update##n(void* view, void* edx)                \
    {                                                                      \
        if (n < VIEW_COUNT)                                                \
            OnViewUpdate(n, view);                                         \
        if (g_origSlot[n])                                                 \
            ((tUpdate)g_origSlot[n])(view, 0);                             \
    }                                                                      \
    static void* __fastcall Tip##n(void* view, void* edx,                  \
                                   void* retBuf, void* element)            \
    {                                                                      \
        if (n < VIEW_COUNT)                                                \
            OnViewUpdate(n, view);                                         \
        if (!g_origSlot[n])                                                \
            return retBuf;                                                 \
        return ((tTooltip)g_origSlot[n])(view, 0, retBuf, element);        \
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
    static int logged = 0;

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

    if (logged < 20)
    {
        ++logged;
        Log("IsValid: '%s' rva=%06X fromList=%d",
            name, caller - g_base, (int)fromList);
    }

    return fromList ? 0 : 1;
}


// ---------------------------------------------------------------
// Установка
// ---------------------------------------------------------------

// Записывает указатель в слот vtable, возвращая прежнее значение.
// Таблицы лежат в .rdata, поэтому только со снятием защиты — иначе
// запись молча не проходит.
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
// Процентный шаг изменения цены
//
// Ванильно шаг фиксирован: по адресу 0x0125B9E0 лежит int64 = 328,
// то есть 0.01 в формате с фиксированной точкой (0.01 * 2^15).
// Цены хранятся там же с 15 битами дробной части.
//
// Ставим перехват прямо перед сравнением, где в ECX:EAX уже лежит
// текущая цена товара, и переписываем константу на долю от неё.
//
// Считаем через умножение, а не сдвиг: сдвиг даёт только степени
// двойки (0.78%, 0.39%, ...), а так процент задаётся любым числом.
// delta = price * MUL >> 16, где MUL подобран под нужную долю.
//
// Почему это безопасно для мультиплеера: арифметика целочисленная,
// от времени и порядка событий не зависит, при одинаковой DLL все
// клиенты получают побитно одинаковый результат. Константу не читает
// никто, кроме этой функции — проверено по ссылкам в Ghidra.
//
// Единственное условие: DLL должна быть одинаковой у всех. Игра
// сверяет файлы мода, но бинарники в контрольную сумму не входят.
// ---------------------------------------------------------------

// Шаг изменения цены в сотых долях процента: 25 = 0.25% в день,
// 100 = 1%, 10 = 0.1%. Это единственное, что здесь стоит править.
static const int PRICE_BASIS_POINTS = 25;

// Множитель для сдвига на 16 бит. Считается на этапе компиляции.
static const DWORD PRICE_MUL =
(DWORD)((PRICE_BASIS_POINTS * 65536LL) / 10000);

static const DWORD RVA_PRICE_HOOK = 0x82BA9;   // SAR EDX,0Fh
static const DWORD RVA_PRICE_RESUME = 0x82BAE;   // SUB EDI,[delta]
static const DWORD RVA_PRICE_DELTA = 0xE5B9E0;  // int64, младшее слово

// Пять байт, которые мы перекрываем: SAR EDX,0Fh + MOV EDI,EAX.
// Не совпали — версия игры другая, и патчить нельзя: молчаливое
// расхождение бинарников хуже отсутствия правки.
static const unsigned char PRICE_SIG[5] = { 0xC1, 0xFA, 0x0F, 0x8B, 0xF8 };

static bool InstallPriceDelta()
{
    unsigned char* hook = (unsigned char*)(g_base + RVA_PRICE_HOOK);

    if (memcmp(hook, PRICE_SIG, sizeof(PRICE_SIG)) != 0)
    {
        Log("PriceDelta: сигнатура не совпала (%02X %02X %02X %02X %02X) - не патчим",
            hook[0], hook[1], hook[2], hook[3], hook[4]);
        return false;
    }

    unsigned char* cave = (unsigned char*)VirtualAlloc(
        0, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    if (!cave)
    {
        Log("PriceDelta: не удалось выделить память");
        return false;
    }

    DWORD deltaLo = g_base + RVA_PRICE_DELTA;
    DWORD deltaHi = deltaLo + 4;

    int n = 0;

    // Восстанавливаем то, что перекрыли прыжком.
    cave[n++] = 0xC1; cave[n++] = 0xFA; cave[n++] = 0x0F;   // sar edx, 0Fh
    cave[n++] = 0x8B; cave[n++] = 0xF8;                     // mov edi, eax

    // EAX и ECX — цена, EDX — целевая цена, всё нужно дальше.
    // MUL затирает EDX, поэтому сохраняем и его.
    cave[n++] = 0x50;                                       // push eax
    cave[n++] = 0x51;                                       // push ecx
    cave[n++] = 0x52;                                       // push edx

    // delta = price * PRICE_MUL >> 16
    cave[n++] = 0xB9;                                       // mov ecx, PRICE_MUL
    *(DWORD*)(cave + n) = PRICE_MUL; n += 4;
    cave[n++] = 0xF7; cave[n++] = 0xE1;                     // mul ecx
    cave[n++] = 0x0F; cave[n++] = 0xAC; cave[n++] = 0xD0;
    cave[n++] = 0x10;                                       // shrd eax, edx, 16
    cave[n++] = 0xC1; cave[n++] = 0xEA; cave[n++] = 0x10;   // shr edx, 16

    // Нижняя граница: у совсем дешёвых товаров доля округляется в
    // ноль, и цена застыла бы навсегда. Поднимаем такой случай до 1.
    cave[n++] = 0x85; cave[n++] = 0xD2;                     // test edx, edx
    cave[n++] = 0x75; cave[n++] = 0x05;                     // jnz store
    cave[n++] = 0x85; cave[n++] = 0xC0;                     // test eax, eax
    cave[n++] = 0x75; cave[n++] = 0x01;                     // jnz store
    cave[n++] = 0x40;                                       // inc eax

    // store:
    cave[n++] = 0xA3;                                       // mov [deltaLo], eax
    *(DWORD*)(cave + n) = deltaLo; n += 4;
    cave[n++] = 0x89; cave[n++] = 0x15;                     // mov [deltaHi], edx
    *(DWORD*)(cave + n) = deltaHi; n += 4;

    cave[n++] = 0x5A;                                       // pop edx
    cave[n++] = 0x59;                                       // pop ecx
    cave[n++] = 0x58;                                       // pop eax

    // Возврат на инструкцию сразу за перехватом.
    cave[n++] = 0xE9;
    *(DWORD*)(cave + n) = (g_base + RVA_PRICE_RESUME) - (DWORD)(cave + n + 4);
    n += 4;

    // Сам прыжок в пещеру.
    unsigned char patch[5];
    patch[0] = 0xE9;
    *(DWORD*)(patch + 1) = (DWORD)cave - ((DWORD)hook + 5);

    DWORD oldProtect = 0;
    if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        Log("PriceDelta: не удалось снять защиту");
        return false;
    }

    memcpy(hook, patch, sizeof(patch));
    VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

    Log("PriceDelta: %d сотых процента, множитель %u, пещера %08X, %d байт",
        PRICE_BASIS_POINTS, PRICE_MUL, (DWORD)(DWORD_PTR)cave, n);
    return true;
}



// ---------------------------------------------------------------
// Байтовые правки exe
//
// Смещения взяты из exe-модов ZombieFreak115 и заданы так же, как
// у него — как смещения в ФАЙЛЕ, а не адреса в памяти. Переводим их
// через таблицу секций PE прямо в рантайме, поэтому ничего гадать
// про раскладку exe не нужно.
//
// expect — ожидаемые байты. Не совпали — правка не ставится и в лог
// идёт строка: на чужой версии игры лучше отказаться, чем записать
// байты не туда и получить молчаливое расхождение.
//
// ВНИМАНИЕ про мультиплеер: эти правки меняют симуляцию и проверку
// допустимости действий. При разных DLL у игроков расхождение
// неизбежно, причём для целей войны оно проявится не сразу, а при
// подсчёте условий мира.
// ---------------------------------------------------------------

struct BytePatch
{
    const char* name;
    DWORD         fileOffset;
    int           len;
    unsigned char expect[8];   // нули + expectLen 0 = не проверять
    int           expectLen;
    unsigned char replace[8];
    bool          enabled;
};

static BytePatch EXE_PATCHES[] =
{
    // Постоянно включённый debug alwaysaddwargoal.
    { "always_add_wargoals", 0x137EFF, 1, { 0x00 }, 1, { 0x02 }, true },

    // Сухопутные: NOP на инструкции, переносящей ослабленное значение
    // снабжения на следующую бригаду в стеке.
    // Было: mov [esp+20h], ecx — пересылка ослабленного снабжения
    // на следующую бригаду в стеке.
    { "land_reinforce",      0x1C809B, 4, { 0x89, 0x4C, 0x24, 0x20 }, 4,
      { 0x90, 0x90, 0x90, 0x90 }, true },

      // Флот: 89 -> 8B, направление пересылки меняется на обратное,
      // и в следующий корабль попадает исходное значение снабжения.
      { "naval_reinforce",     0x1C7F1C, 1, { 0x89 }, 1, { 0x8B }, true },
};

static const int EXE_PATCH_COUNT = sizeof(EXE_PATCHES) / sizeof(EXE_PATCHES[0]);


// Смещение в файле -> RVA, по таблице секций загруженного модуля.
// Возвращает 0, если смещение не попало ни в одну секцию.
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
        {
            Log("Patch '%s': выключен", bp.name);
            continue;
        }

        DWORD rva = FileOffsetToRVA(bp.fileOffset);
        if (!rva)
        {
            Log("Patch '%s': смещение %06X вне секций", bp.name, bp.fileOffset);
            continue;
        }

        unsigned char* at = (unsigned char*)(g_base + rva);

        // Прежние байты в лог — по ним потом впишем expect.
        char before[64] = { 0 };
        for (int b = 0; b < bp.len && b < 8; ++b)
        {
            char one[8];
            sprintf_s(one, sizeof(one), "%02X ", at[b]);
            strcat_s(before, sizeof(before), one);
        }

        if (bp.expectLen > 0 && memcmp(at, bp.expect, bp.expectLen) != 0)
        {
            Log("Patch '%s': сигнатура не совпала, было %s- не патчим",
                bp.name, before);
            continue;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(at, bp.len, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            Log("Patch '%s': не удалось снять защиту", bp.name);
            continue;
        }

        memcpy(at, bp.replace, bp.len);
        VirtualProtect(at, bp.len, oldProtect, &oldProtect);

        Log("Patch '%s': файл %06X -> rva %06X, было %s", bp.name,
            bp.fileOffset, rva, before);
    }
}



// ---------------------------------------------------------------
// Замер: какие функции вообще вызываются
//
// Ставит на каждую из перечисленных функций перехват, который только
// пишет в лог первый вызов вместе с регистрами. Ничего не меняет.
// Нужен, чтобы не гадать, какая точка срабатывает при открытии окна.
//
// Все кандидаты начинаются одинаково: 55 8B EC 6A FF, то есть
// push ebp / mov ebp,esp / push -1 — ровно пять байт под переход.
// ---------------------------------------------------------------

#define ENABLE_TRACE 0

struct TracePoint
{
    const char* name;
    DWORD       rva;
};

static const TracePoint TRACE_POINTS[] =
{
    { "BuildFactoryWindow::ctor", 0x2F95B0 },  // FUN_006f95b0
    { "CreateBuildFactory",       0x2FE390 },  // FUN_006fe390
    { "BuildFactory::slot0",      0x2F9860 },  // FUN_006f9860
    { "BuildFactory::populate",   0x2F9920 },  // FUN_006f9920
};

static const int TRACE_COUNT = sizeof(TRACE_POINTS) / sizeof(TRACE_POINTS[0]);

static const unsigned char TRACE_SIG[5] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };

static int g_traceSeen[8] = { 0 };

static void __cdecl TraceHit(int index, void* esi, void* ecx, void* arg1)
{
    if (index < 0 || index >= TRACE_COUNT)
        return;

    if (g_traceSeen[index] >= 2)
        return;

    ++g_traceSeen[index];

    Log("TRACE '%s': esi=%08X ecx=%08X arg1=%08X",
        TRACE_POINTS[index].name,
        (DWORD)(DWORD_PTR)esi, (DWORD)(DWORD_PTR)ecx, (DWORD)(DWORD_PTR)arg1);
}


static void InstallTrace()
{
    for (int i = 0; i < TRACE_COUNT; ++i)
    {
        unsigned char* hook = (unsigned char*)(g_base + TRACE_POINTS[i].rva);

        if (memcmp(hook, TRACE_SIG, sizeof(TRACE_SIG)) != 0)
        {
            Log("TRACE '%s': пролог другой (%02X %02X %02X %02X %02X) - пропуск",
                TRACE_POINTS[i].name,
                hook[0], hook[1], hook[2], hook[3], hook[4]);
            continue;
        }

        unsigned char* cave = (unsigned char*)VirtualAlloc(
            0, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

        if (!cave)
            continue;

        int n = 0;

        cave[n++] = 0x60;                                   // pushad
        cave[n++] = 0xFF; cave[n++] = 0x74;                 // push [esp+0x24]
        cave[n++] = 0x24; cave[n++] = 0x24;                 //   = первый аргумент
        cave[n++] = 0x51;                                   // push ecx
        cave[n++] = 0x56;                                   // push esi
        cave[n++] = 0x6A; cave[n++] = (unsigned char)i;     // push i
        cave[n++] = 0xB8;                                   // mov eax, TraceHit
        *(DWORD*)(cave + n) = (DWORD)(DWORD_PTR)&TraceHit; n += 4;
        cave[n++] = 0xFF; cave[n++] = 0xD0;                 // call eax
        cave[n++] = 0x83; cave[n++] = 0xC4; cave[n++] = 0x10;  // add esp, 16
        cave[n++] = 0x61;                                   // popad

        // Перекрытые байты — как есть.
        memcpy(cave + n, TRACE_SIG, sizeof(TRACE_SIG));
        n += sizeof(TRACE_SIG);

        cave[n++] = 0xE9;                                   // jmp обратно
        *(DWORD*)(cave + n) =
            (g_base + TRACE_POINTS[i].rva + 5) - (DWORD)(cave + n + 4);
        n += 4;

        unsigned char patch[5];
        patch[0] = 0xE9;
        *(DWORD*)(patch + 1) = (DWORD)cave - ((DWORD)hook + 5);

        DWORD oldProtect = 0;
        if (!VirtualProtect(hook, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
            continue;

        memcpy(hook, patch, sizeof(patch));
        VirtualProtect(hook, sizeof(patch), oldProtect, &oldProtect);

        Log("TRACE '%s': перехват на rva %06X", TRACE_POINTS[i].name,
            TRACE_POINTS[i].rva);
    }
}


static bool Install()
{
    g_base = (DWORD)GetModuleHandleA(NULL);
    if (!g_base)
        return false;

    g_fnOnMakeDecision = (void*)(g_base + RVA_ONMAKEDECISION);

    Log("---- Install ---- версия %s", MOD_VERSION);
    Log("base   = %08X", g_base);
    Log("onMake = %08X", (DWORD)g_fnOnMakeDecision);

    if (VIEW_COUNT > MAX_VIEWS)
        Log("Install: видов больше %d, лишние не заработают", MAX_VIEWS);
    if (BUTTON_COUNT > MAX_BUTTONS)
        Log("Install: кнопок больше %d, лишние не заработают", MAX_BUTTONS);

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

#if ENABLE_BUTTONS
    for (int i = 0; i < VIEW_COUNT && i < MAX_VIEWS; ++i)
    {
        void* thunk = VIEWS[i].tooltipSlot ? TOOLTIP_THUNKS[i] : UPDATE_THUNKS[i];

        bool ok = PatchSlot(VIEWS[i].rvaVtable, VIEWS[i].slot,
            thunk, &g_origSlot[i]);
        Log("patch %s: слот %d = %d, orig = %08X",
            VIEWS[i].name, VIEWS[i].slot, (int)ok,
            (DWORD)(DWORD_PTR)g_origSlot[i]);
    }
#else
    Log("patch видов отключён");
#endif

#if ENABLE_INDICATORS
#if ENABLE_TRACE
    InstallTrace();
#else
    InstallBfCtorHook();
#endif
#endif

#if ENABLE_DECISION_FILTER
    bool okDecision = PatchSlot(RVA_VTABLE_DECISION, VT_SLOT_ISVALID,
        (void*)&MyDecisionIsValid, (void**)&g_origIsValid);
    Log("patch decision = %d, orig = %08X  (ожидаем %08X)",
        (int)okDecision, (DWORD)(DWORD_PTR)g_origIsValid, g_base + 0x631490);
#else
    Log("patch decision = отключён");
#endif

#if ENABLE_EXE_PATCHES
    InstallExePatches();
#else
    Log("ExePatches = отключены");
#endif

#if ENABLE_PRICE_DELTA
    InstallPriceDelta();
#else
    Log("PriceDelta = отключён");
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