#pragma once
// Alle Texte des Monitors, Deutsch und Russisch. Wo die App denselben Begriff
// hat, steht hier derselbe Wortlaut wie in www/index.html.

enum Txt {
  T_ENERGIEFLUSS, T_SOLAR, T_NETZ, T_NETZ_AKKU, T_INS_HAUS, T_AKKU,
  T_AKKU_RUHT, T_AKKU_LAEDT, T_AKKU_ENTLAEDT,
  T_NOCH_HM, T_NOCH_M, T_VOLL_HM, T_VOLL_M, T_GESPEICHERT_FMT,
  T_OFFGRID, T_AUS, T_AN, T_BEREIT, T_KWH,
  T_ZUSTAND, T_GESPEICHERT, T_PACKSPANNUNG, T_AKKUSTROM, T_ENTLADEN_BIS,
  T_ZELLEN, T_IN_REIHE, T_HOECHSTE, T_NIEDRIGSTE, T_SPREIZUNG,
  T_TEMPERATUR, T_WECHSELRICHTER, T_LUEFTER, T_STUFE, T_HEIZUNG,
  T_MODUS, T_MODUS_NETZ, T_MODUS_HAUS, T_AUSGANGSLIMIT, T_WLAN, T_VERLUST,
  T_HEUTE, T_LEG_LAEDT, T_SOLAR_HEUTE, T_AUS_NETZ, T_VERLAUF_WARTET,
  T_EINSTELLUNGEN, T_SPEICHER, T_GEFUNDEN_UNTER, T_AENDERN, T_MODULLEISTUNG, T_KAPAZITAET,
  T_SPRACHE, T_HELL_NACHT, T_HELL_FMT, T_ZEITZONE,
  T_WLAN_WAEHLEN, T_SCHRITT, T_NETZE_GEFUNDEN, T_SUCHE_NETZE, T_WEITERE, T_NAME_EINTIPPEN,
  T_NUR24, T_NEU_SUCHEN, T_OFFEN,
  T_PASSWORT, T_NETZNAME, T_ZEICHEN_ZEIGEN, T_ZEICHEN_N, T_VERBINDEN, T_WEITER, T_LEERTASTE,
  T_UEBERNEHMEN, T_VERBINDE, T_VERB_FEHLER, T_ZURUECK, T_NOCHMAL,
  T_SPEICHER_SUCHEN, T_SUCHE_GERAETE, T_N_GEFUNDEN, T_KEIN_GERAET, T_IP_EINTIPPEN, T_IP_ADRESSE,
  T_KEINE_DATEN, T_WARTE, T_MP_PV, T_ANTIPPEN,
  T_FIRMWARE, T_INSTALLIERT, T_VERFUEGBAR, T_AKTUELL, T_PRUEFE, T_JETZT_PRUEFEN, T_INSTALLIEREN,
  T_UPD_HINWEIS, T_LADE, T_NEUSTART, T_UPD_PILLE, T_UPD_FEHLER, T_NICHT_GEPRUEFT,
  T_ANZ
};

static const char *const TEXTE[2][T_ANZ] = {
  { // Deutsch
    "Energiefluss", "Solar", "Netz", "Netz \xE2\x86\x92 Akku", "Ins Haus", "Akku",
    "Akku ruht", "Akku lädt", "Akku entlädt",
    "noch %d h %d min", "noch %d min", "voll in %d h %d min", "voll in %d min", "%s kWh gespeichert",
    "Off-Grid", "aus", "an", "bereit", "kWh",
    "Zustand", "Gespeichert", "Packspannung", "Akkustrom", "Entladen bis",
    "Zellen", "%d in Reihe", "Höchste", "Niedrigste", "Spreizung",
    "Temperatur", "Wechselrichter", "Lüfter", "Stufe %d", "Heizung",
    "Modus", "Laden aus Netz", "Abgabe ins Haus", "Ausgangslimit", "WLAN", "Verlust im Gerät",
    "Heute", "Akku lädt", "Solar heute", "Aus dem Netz",
    "Der Verlauf beginnt, sobald Daten und Uhrzeit da sind.",
    "Einstellungen", "Speicher", "gefunden unter", "ändern", "Modulleistung je Eingang (Wp)",
    "Akku-Kapazität (Wh)",
    "Sprache", "Helligkeit nachts", "%d %% ab 22:30", "Zeitzone",
    "WLAN wählen", "Schritt %d von 3", "%d Netze gefunden", "Suche Netze …", "Weitere anzeigen",
    "Name eintippen",
    "Nur 2,4 GHz — der Monitor kann kein 5 GHz", "Neu suchen", "offen",
    "Passwort", "Netzname", "Zeichen zeigen", "%d Zeichen", "Verbinden", "Weiter", "Deutsch",
    "Übernehmen", "Verbinde mit %s …", "Verbindung fehlgeschlagen. Bitte das Passwort prüfen.",
    "Zurück", "Nochmal",
    "Speicher suchen", "Suche im WLAN …", "%d gefunden",
    "Kein Speicher gefunden. Ist er im selben WLAN?", "IP eintippen", "IP-Adresse",
    "keine Daten", "Warte auf Daten …", "PV%d (Wp)", "antippen zum Wählen",
    "Firmware", "Installiert", "Verfügbar", "Kein neueres Build", "Prüfe GitHub …", "Jetzt prüfen",
    "Installieren",
    "WLAN, Speicher und Verlauf bleiben erhalten. Der Monitor startet danach neu.",
    "Lade Build %d … %d %%", "Fertig – Neustart …", "Update: Build %d", "Update fehlgeschlagen",
    "noch nicht geprüft"
  },
  { // Русский
    "Поток энергии", "Солнце", "Сеть", "Сеть \xE2\x86\x92 АКБ", "В дом", "Аккумулятор",
    "АКБ в покое", "АКБ заряжается", "АКБ разряжается",
    "ещё %d ч %d мин", "ещё %d мин", "полный через %d ч %d мин", "полный через %d мин",
    "накоплено %s кВт·ч",
    "Розетка", "выкл.", "вкл.", "готова", "кВт·ч",
    "Состояние", "Накоплено", "Напряжение", "Ток батареи", "Разряжать до",
    "Ячейки", "%d посл.", "Максимальная", "Минимальная", "Разброс",
    "Температура", "Инвертор", "Вентилятор", "ступень %d", "Подогрев",
    "Режим", "Зарядка от сети", "Выдача в дом", "Лимит выдачи", "Wi-Fi", "Потери в устройстве",
    "Сегодня", "Заряд АКБ", "Солнце сегодня", "Из сети",
    "Журнал начнётся, когда будут данные и время.",
    "Настройки", "Накопитель", "адрес", "изменить", "Мощность модулей на вход (Вт)",
    "Ёмкость АКБ (Вт·ч)",
    "Язык", "Яркость ночью", "%d %% с 22:30", "Часовой пояс",
    "Выбор Wi-Fi", "Шаг %d из 3", "Найдено сетей: %d", "Поиск сетей …", "Показать ещё",
    "Ввести имя",
    "Только 2,4 ГГц — монитор не работает с 5 ГГц", "Искать снова", "открытая",
    "Пароль", "Имя сети", "Показать символы", "Символов: %d", "Подключить", "Далее", "Пробел",
    "Применить", "Подключение к %s …", "Не удалось подключиться. Проверьте пароль.",
    "Назад", "Ещё раз",
    "Поиск накопителя", "Поиск в сети …", "Найдено: %d",
    "Накопитель не найден. Он в той же сети?", "Ввести IP", "IP-адрес",
    "нет данных", "Ожидание данных …", "PV%d (Вт)", "нажмите, чтобы выбрать",
    "Прошивка", "Установлена", "Доступна", "Новее нет", "Проверка GitHub …", "Проверить",
    "Установить",
    "Wi-Fi, накопитель и журнал сохранятся. Затем монитор перезапустится.",
    "Загрузка сборки %d … %d %%", "Готово — перезапуск …", "Обновление: сборка %d", "Ошибка обновления",
    "ещё не проверено"
  }
};

static int sprache = 0;   // 0 Deutsch, 1 Русский
static inline const char *T(int k) { return TEXTE[sprache][k]; }

// Russische Mehrzahl: 1 вход, 2-4 входа, 5+ входов (wie z_inputs_n in der App)
static const char *ruZahl(int n, const char *eins, const char *wenige, const char *viele) {
  int z = n % 100, e = n % 10;
  if (z >= 11 && z <= 14) return viele;
  return e == 1 ? eins : (e >= 2 && e <= 4) ? wenige : viele;
}

struct Zone { const char *de, *ru, *tz; };
static const Zone ZONEN[] = {
  {"Deutschland", "Германия", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Kaliningrad", "Калининград", "EET-2"},
  {"Moskau", "Москва", "MSK-3"},
  {"Samara", "Самара", "<+04>-4"},
  {"Jekaterinburg", "Екатеринбург", "<+05>-5"},
  {"Kasachstan", "Казахстан", "<+05>-5"},
  {"Omsk", "Омск", "<+06>-6"},
  {"Nowosibirsk", "Новосибирск", "<+07>-7"},
  {"Irkutsk", "Иркутск", "<+08>-8"},
  {"Ukraine", "Украина", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
};
static const int ZONEN_ANZ = sizeof(ZONEN) / sizeof(ZONEN[0]);
