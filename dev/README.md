# Локальный сервер RPS.WoWCore: сборка, запуск, CLion. Инструкция для человека

Ядро собирается на Windows через `make` или CLion. Оба используют одни и те же CMake-пресеты и одну папку сборки `build/dev`, поэтому ничего не компилируется дважды.

Ядру нужны 5 баз MariaDB: `wow_auth`, `wow_characters`, `wow_world`, `wow_hotfixes`, `wow_roleplay`. Ядро подключается к ним под своим пользователем `wow_core`. Где живут базы, задаёт `DB_MODE` в `dev/local.mk`, может быть два варианта:
- `docker` - в контейнере сайта `noblegarden-mariadb` на порту `3308`. Подходит, если на локалке уже поднят сайт.
- `native` - в MariaDB, установленной прямо на машину, порт `3306`, а сами базы загружаются из дампов. Для этого варианта Docker не нужен, см. раздел "Если на локалке нет сайта с докером".

`make init` сам выбирает `docker`, если находит контейнер сайта, иначе пойдет по варианту`native`.

## Быстрый старт

1. Проверь машину, запусти из корня проекта:
   ```
   make doctor
   ```
   или
   ```
   dev\doctor.cmd
   ```
   Команда проверяет инструменты, либы, бд и порты. Под каждой проблемой стоит строка `FIX:` с командой установки или настройкой, которую надо поправить. Выполняй FIX сверху вниз и запускай doctor снова, пока не исчезнут строки `FAIL`.

2. Сделать всё остальное одной командой:
   ```
   make setup
   ```
   Команда по очереди делает все шаги из раздела "Первая настройка". Она останавливается на первой ошибке. Исправь ошибку и запусти `make setup` ещё раз: уже сделанное повторно не ломается.

3. Положить данные клиента и войти в игру - см. раздел "Первый вход в игру".

Если работаешь с AI-агентом, дай ему файл `dev/AGENT-SETUP.md`: там пошаговый план для дешёвой модели.

## Как это выглядит после настройки

```
RPS.WoWCore/
  Makefile                       все команды, список: make help
  CMakePresets.json              общие настройки сборки (в git)
  CMakeUserPresets.json          пути этой машины (НЕ в git, создаёт make init)
  dev/
    doctor.cmd                   проверка машины, работает и без make
    local.mk                     пути, порты, пароль бд (НЕ в git, создаёт make init)
    local.my.cnf                 логин mysql.exe для make db-* (НЕ в git, создаётся сам)
    dumps/                       дампы для DB_MODE = native (НЕ в git, кладёшь сама)
    backups/                     результат make db-backup (НЕ в git)
    scripts/                     скрипты, которые вызывает Makefile
    sql/                         SQL для пользователя wow_core и локального реалма
  build/dev/bin/RelWithDebInfo/  папка запуска
    bnetserver.exe, worldserver.exe, *.pdb
    bnetserver.conf, worldserver.conf     создаёт make conf
    libmysql.dll, libssl*, libcrypto*     кладёт make dlls
    lua_scripts/NobleNextLua     ссылка (junction) на репозиторий NobleNextLua
    lua_scripts/AIO/             кладёшь сама
```

Данные клиента лежат отдельно, вне репозитория. По умолчанию это папка `wow-data` рядом с репозиторием, путь задаётся в `DATA_DIR` в `dev/local.mk`:

```
wow-data/
  dbc/  gt/  cameras/  maps/  vmaps/  mmaps/
```

## Системные требования

- Visual Studio 2022 (любая редакция) с компонентом "Desktop development with C++".
- CMake 3.24 или новее.
- Boost 1.83 (сборка msvc-14.3, x64), обычно в `C:/local/boost_1_83_0`.
- MySQL 8.0: из него нужны только `lib/libmysql.dll` и `bin/mysql.exe`.
- OpenSSL 3 (Win64).
- GNU make и Git.
- Для бд одно из двух:
  - `DB_MODE = docker`: Docker Desktop и запущенный стек сайта (`noble-site`, `docker/dev`), из него нужен только контейнер MariaDB;
  - `DB_MODE = native`: MariaDB 11.8, установленная на машину, и дампы баз.
- Путь к репозиторию без пробелов и без русских букв.

Что ставить и какой командой, подскажет `dev\doctor.cmd`. `make init` сам находит установленное и записывает пути в `dev/local.mk` и `CMakeUserPresets.json`. Если что-то стоит в нестандартном месте, поправь путь там руками.

## Первая настройка

1. Создать локальные файлы:
   ```
   make init
   ```
   Появятся `dev/local.mk` со случайным паролем для `wow_core` и `CMakeUserPresets.json`. Существующие файлы команда не трогает.

2. Проверить окружение:
   ```
   make doctor
   ```
   Если всё ок, должно выдать `doctor: all required checks passed`. Строка `WARN DataDir` нормальна, пока нет данных клиента.

3. Собрать ядро:
   ```
   make configure build
   ```
   Первая сборка идёт долго, 30-60 минут. Повторные сборки пересобирают только изменённое.

4. Подготовить папку запуска:
   ```
   make prep
   ```
   Команда кладёт DLL рядом с exe, создаёт `worldserver.conf` и `bnetserver.conf` с адресом бд, `DataDir` и `RealmID` и делает ссылку `lua_scripts/NobleNextLua`. Готовые `.conf` она не перезаписывает. Чтобы пересоздать их, запусти `make conf FORCE=1`: старый файл сохранится как `.bak`.

5. Подготовить бд:
   ```
   make db-up db-check
   make db-backup
   make db-user db-realmlist
   ```
   Это шаги для `DB_MODE = docker`. Для `native` порядок другой, см. раздел "Если на локалке нет сайта с докером".
   - `db-up` запускает контейнер MariaDB сайта, если он остановлен, и ждёт статуса healthy.
   - `db-check` показывает базы, строки `realmlist` и SQL-апдейты из репозитория, которых ещё нет в базе.
   - `db-backup` сохраняет все 5 баз в `BACKUP_DIR` (по умолчанию `dev/backups`).
   - `db-user` создаёт пользователя `wow_core` с правами на 5 баз `wow_*`. Пользователь сайта не меняется.
   - `db-realmlist` добавляет локальный реалм `id = 2`, `127.0.0.1:8085`. Строку `id = 1` читает сайт, её команда не трогает.

   Если `db-check` показал pending-файлы, то сделай `make db-backup`, И ТОЛЬКО ПОТОМ `make db-update`. Эта команда один раз прогоняет апдейтер ядра по auth, characters, world и hotfixes и выходит. Пока она работает, пароль `wow_core` виден в списке процессов: ядро передаёт его в `mysql.exe` через командную строку. Для локалки это не страшно.

6. Проверить, что worldserver видит бд:
   ```
   make world-db
   ```
   worldserver подключается ко всем 5 базам и сразу выходит. Если всё ок, в конце должно выдать `exit code 0`.

7. Положить данные клиента в `DATA_DIR` - см. раздел "Первый вход в игру".

8. Запустить серверы:
   ```
   make run-bnet
   make run-world
   ```
   Каждый сервер открывается в своём окне. Сначала bnetserver, потом worldserver.

## Если на локалке нет сайта с докером

Docker и сайт не обязательны: базы могут жить в MariaDB, установленной прямо на машину, а данные для них берутся из дампов.

1. Поставить MariaDB 11.8:
   ```
   winget install --source winget -e --id MariaDB.Server --version 11.8.2.0 --interactive
   ```
   Откроется обычный установщик. Задай в нём пароль root и запомни его, он понадобится дальше. Порт оставь `3306`. Если на `3306` уже стоит MySQL, выбери другой порт, например `3307`, и пропиши его в `DB_PORT` в `dev/local.mk`. Сама MySQL для бд не годится: дампы с MariaDB 11 в неё не загрузятся.

2. Проверить настройки. В `dev/local.mk` должно быть:
   ```
   DB_MODE = native
   DB_PORT = 3306
   MARIADB_DIR = C:/Program Files/MariaDB 11.8
   ```
   `make init` ставит это сам, если не нашёл контейнер сайта. `make doctor` покажет, если что-то не так.

3. Получить дампы. Попроси сами дампы баз у другого разработчика, и положи их в `dev/dumps/`. Подойдут отдельные дампы по базам: `*.sql` или `*.sql.gz`, в имени файла должно быть `auth`, `characters`, `world`, `hotfixes` или `roleplay`. Файлы загружаются по алфавиту.

4. Загрузить базы и всё остальное:
   ```
   make setup
   ```
   Пароль root команда спросит у тебя дважды: для создания пользователя `wow_core` и для загрузки дампов. Пароль нигде не сохраняется.

   Если `make setup` уже был, базы можно загрузить отдельно:
   ```
   make db-user db-import db-realmlist db-check
   ```

Как ведёт себя `make db-import`:
- Дамп грузится только в пустые базы. Базу, в которой уже есть таблицы, команда пропускает и пишет `skip`.
- `make db-import FORCE=1` загружает и в заполненные базы. Таблицы из дампа при этом заменяются, ПОЭТОМУ сначала сделай `make db-backup`.
- Дамп, в котором есть чужие базы (например, системная `mysql`), команда не грузит целиком и пишет, какие базы лишние.
- Если в дампе базы называются без `wow_` (например, просто `auth`), поставь такой префикс в `DB_PREFIX` в `dev/local.mk`.

После загрузки `make db-check` может показать pending-апдейты: это SQL из репозитория, который новее дампа. Прогони их: `make db-backup`, потом `make db-update`.

## Первый вход в игру

Это нужно сделать руками один раз.

1. Извлечь данные клиента. Экстракторы собираются вместе с ядром и лежат в папке запуска: `mapextractor.exe`, `vmap4extractor.exe`, `vmap4assembler.exe`, `mmaps_generator.exe`. Их запускают из папки клиента. Результат (`dbc gt cameras maps vmaps mmaps`) нужно сложить в `DATA_DIR`.

2. Положить AIO в `build/dev/bin/RelWithDebInfo/lua_scripts/AIO/`. Без AIO не будет интерфейса RP-аддонов, но сервер запустится.

3. Взять клиент нужной версии. Реалм ждёт клиент **12.1.5.70448**: это `gamebuild` в `wow_auth.realmlist`. Клиент другой сборки не пустят.

4. Направить клиент на локалку. В `WTF/Config.wtf` клиента должна быть строка `SET portal "127.0.0.1"`. Retail-клиенту для входа на свой сервер нужен лаунчер, который подменяет адрес и ключи. Его настраивают так же, как для обычного сервера проекта.

5. Подготовить аккаунт. В бд уже есть аккаунты из дампа, и их пароли такие же, как на основном сервере. Чтобы завести свой, набери в окне worldserver:
   ```
   bnetaccount create test@local.dev pass1234
   bnetaccount listgameaccounts test@local.dev
   account set seclevel 2#1 3 -1
   ```
   Вторая команда покажет имя игрового аккаунта, например `2#1`. Это имя нужно подставить в третью команду: она выдаёт права GM (уровень 3) на всех реалмах.

6. Войти: логин - email из `bnetaccount create`, реалм - `NobleNext Local`.

## Запуск вручную, без make

Все команды выполняются из папки запуска `build/dev/bin/RelWithDebInfo`. Серверы ищут свои `.conf` и сертификаты именно там.

```
cd build\dev\bin\RelWithDebInfo
start bnetserver.exe
start worldserver.exe
```

Остановить worldserver: в его окне набрать `server shutdown 1` или нажать Ctrl+C. bnetserver закрывается Ctrl+C. Если окна зависли, то `make stop` убьёт оба процесса.

Перед ручным запуском контейнер MariaDB должен быть запущен: `docker start noblegarden-mariadb`.

## Повседневные нужды

| Задача | Команда |
|---|---|
| Пересобрать после правок | `make build` |
| Запустить серверы | `make run` (bnet и world в своих окнах) |
| Остановить серверы | `server shutdown 1` в окне worldserver, или `make stop` |
| Чекнуть, что бд жива и видна ядру | `make db-check`, `make world-db` |
| Обновить конфиги после смены `dev/local.mk` | `make conf FORCE=1` |
| Сделать бэкап баз перед экспериментом | `make db-backup` |
| Прогнать тесты | `make test` |
| Собрать Debug (медленный в игре, удобнее отлаживать) | `make build-debug`, папка `build/dev-debug` |
| Подготовить и запустить Debug | `make prep run PRESET=dev-debug CONFIG=Debug`: без этих переменных команды работают с `build/dev` |
| Начать сборку с нуля | `make clean`, потом `make configure build` |

## CLion

1. Настроить тулчейн. В `Settings > Build, Execution, Deployment > Toolchains` добавь тулчейн `Visual Studio` и назови его **`VS2022`**: это имя записано в `CMakeUserPresets.json`.
   - Toolset: твоя Visual Studio 2022, Architecture: `amd64`.
   - CMake: тот же, что в `CMAKE` в `dev/local.mk`.
   - Build Tool: тот же ninja, что в `CMAKE_MAKE_PROGRAM` в `CMakeUserPresets.json`. Можно оставить `Detected`.

2. Открыть папку репозитория через `File > Open`, не через `New Project`. На вопрос "Open as" выбрать **CMake project**. Затем в `Settings > Build, Execution, Deployment > CMake` включи профиль `dev`, он придёт из пресетов. Стандартный профиль `Debug` выключи, иначе CLion соберёт всё второй раз в `cmake-build-debug`.

3. Настроить запуск. В конфигурациях `worldserver` и `bnetserver` укажи `Working directory` = `<репозиторий>\build\dev\bin\RelWithDebInfo`. Без этого сервер не найдёт свой `.conf`.

4. Отладить. Поставь брейкпоинт, например в `StartDB` в `src/server/worldserver/Main.cpp`, и запусти Debug `worldserver`. Отладка работает и на RelWithDebInfo, но часть переменных будет "optimized out". Для точной отладки используй профиль `dev-debug`.

Важно:
- Не запускай `make configure`, пока CLion перезагружает CMake: оба пишут в `build/dev`.
- `make stop` убивает и worldserver, который сейчас отлаживается в CLion.
- `make run` и запуск из CLion одновременно не уживутся: оба хотят порты 1119 и 8085.
- Без CLion отладить можно через Visual Studio: `make debug`.

## Если что-то не так

| Что видно | Что сделать |
|---|---|
| `make doctor` пишет FAIL | Выполнить команду из строки `FIX:` под ним и запустить doctor снова |
| Нет make, `make` не найден | `dev\doctor.cmd` работает без make и подскажет, как его поставить. После установки открой новый терминал |
| doctor: `DB port 3306 not confirmed as MariaDB` | На порту стоит MySQL. Поставь MariaDB на другой порт и пропиши его в `DB_PORT` |
| `make db-import`: `has other DBs` | В дампе чужие базы. Нужен дамп только 5 баз `wow_*`, например результат `make db-backup` |
| `make db-import`: `no *.sql or *.sql.gz` | Положи дампы в `dev/dumps/` |
| `container ... state:` пусто или не healthy | Поднять стек сайта по его README, потом `make db-up` |
| `connect as wow_core ... FAILED` | `make db-user`. Это нужно и после пересоздания volume сайта: пользователь живёт в volume |
| worldserver пишет про порт 3306 или чужую бд | `.conf` устарел: `make conf FORCE=1` |
| worldserver падает на старте с ошибкой prepare statement | Схема бд отстаёт от кода: `make db-check`, `make db-backup`, `make db-update` |
| Порт 1119, 8081 или 8085 занят | `netstat -ano \| findstr :8085` покажет PID. Закрыть процесс или сменить порт в `.conf` |
| Порт 3308 занят | Сменить `NOBLE_MARIADB_HOST_PORT` в `.env` сайта и `DB_PORT` в `dev/local.mk`, потом `make conf FORCE=1` |
| Не стартует exe, нет DLL | `make dlls` |
| worldserver останавливается на `DataDir` / `dbc` | Нет данных клиента в `DATA_DIR`, это ожидаемо до шага "Первый вход в игру" |
| `make conf` пишет `duplicate key ... commented out` | Это нормально. В `worldserver.conf.dist` ключи `Housing.*` повторяются дважды, а ядро падает на дублях, поэтому второе вхождение закомментировано |
| `make test` падает на `\|Hachievement validation` | Известный падающий тест ядра (`tests/game/Hyperlinks.cpp`), к настройке не относится |
| Сборка: `command line too long` | В `CMakePresets.json` должно стоять `CMAKE_NINJA_FORCE_RESPONSE_FILE`; после правки - `make configure` |

В режиме `docker` импорта бд тут нет специально: базы приходят из volume сайта.

Нельзя запускать: `docker compose down -v`, `docker volume prune`, `--remove-orphans` - это сносит базы сайта.
