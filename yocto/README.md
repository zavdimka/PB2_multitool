# PocketBeagle 2 Yocto image

Минимальный загрузочный образ для PocketBeagle 2 (AM6254), рассчитанный на
загрузку с microSD. Конфигурация основана на Yocto Project `scarthgap` LTS и
upstream-слоях `poky`, `meta-openembedded`, `meta-arm` и `meta-ti`.

## Что оптимизировано

- BusyBox init и mdev вместо SysV init/udev/systemd.
- U-Boot `CONFIG_BOOTDELAY=0`.
- GRUB/EFI timeout равен нулю.
- Тихая командная строка ядра: `quiet loglevel=3`.
- Нет SSH, package manager, локалей и development tools в rootfs.
- Сохранён UART getty на штатном debug UART (`ttyS2`, 115200 бод).

## BB2_multitool

Штатный DTB `k3-am62-pocketbeagle2.dtb` дополнен распиновкой платы
`BB2_multitool`. Поэтому U-Boot выбирает тот же DTB автоматически, без
дополнительного overlay и без лишнего шага при загрузке.

Включены существующие драйверы ядра PRUSS/remoteproc/RPMsg и ADS8688A,
контроллеры SPI0, SPI2, I2C2, I2C3 и UART0. Образ уже содержит прошивки
`am62x-pru0-fw`, `am62x-pru1-fw` и сервис `multitoold`; ядра запускаются
сервисом по команде, после чего проверяются через RPMsg HELLO.

IO0..IO2 и IO4..IO7 настроены как PRU1 GPO, IO3 — как PRU1 GPI для TDO;
IO1 динамически переключается GPO/GPI для SWDIO. IO8..IO11 — PRU0 GPI; линии DIR остаются PRU-выходами:

| Канал | Данные | Направление |
| --- | --- | --- |
| IO0 | P2.08 / PRU1.3 | P2.06 / PRU1.2 |
| IO1 | P2.24 / PRU1.6 | P2.10 / PRU1.4 |
| IO2 | P2.30 / PRU1.12 | P2.28 / PRU1.15 |
| IO3 | P2.34 / PRU1.14 | P2.32 / PRU1.11 |
| IO4 | P2.19 / PRU1.16 | P2.17 / PRU1.19 |
| IO5 | P2.35 / PRU1.8 | P2.33 / PRU1.7 |
| IO6 | P1.02 / PRU1.0 | P1.20 / PRU1.5 |
| IO7 | P1.35 / PRU1.1 | P1.36 / PRU1.9 |
| IO8 | P2.02 / PRU0.8 | P2.04 / PRU0.9 |
| IO9 | P2.18 / PRU0.17 | P2.22 / PRU0.18 |
| IO10 | P1.29 / PRU0.7 | P2.20 / PRU0.12 |
| IO11 | P1.33 / PRU0.1 | P1.31 / PRU0.4 |

Остальная задействованная периферия схемы:

- ADS8688A подключён к SPI2 на P1.06/P1.08/P1.10/P1.12 (CS0).
- Модуль LCDWiki MSP4031 подключён разъёмом X2: ST7796S использует SPI0
  на P2.25/P2.27/P2.29/P2.31, `SPI0_RESET` на P1.21, `SPI0_RS`
  на P1.23 и `LED` на P2.36; FT6336U использует I2C3 и `TP_INT` на P2.05.
  X2.14 (`SD_CS` модуля) на плате не подключён, поэтому слот microSD модуля
  намеренно не активируется и не конфликтует с загрузочной картой PocketBeagle 2.
- I2C2 выведен на P1.26/P1.28, I2C3 — на P2.09/P2.11; `TP_INT` — P2.05.
- `PULL_4_5`, `PULL_6_7`, `PULL_8_9`, `PULL_10_11` назначены соответственно
  P1.04, P1.34, P1.27, P1.25; LED назначен P2.36.

На I2C2 также подключены силовые микросхемы со схемы:

- INA238 по адресу `0x40`, измерительный шунт R59 — `0.1 Ом`.
- TPS55289 по адресу `0x75`: физический R59 имеет `0.1 Ом`, но из-за
  дифференциального делителя 2:1 микросхема видит эффективные `0.05 Ом`.

### MSP4031: дисплей и тачскрин

Для ST7796S используется штатный DRM-драйвер `panel-mipi-dbi-spi` в режиме
RGB565, 320×480, SPI 32 МГц. Инициализация LCDWiki встроена в ядро как
`lcdwiki,msp4031.bin`, поэтому она доступна до монтирования rootfs. Подсветка
управляется через `/sys/class/backlight/` и GPIO P2.36.

FT6336U работает через существующий `edt-ft5x06` с добавленным точным
`focaltech,ft6336`, адрес I2C — `0x38`, максимум два касания. События доступны
как `/dev/input/event*`. Линии LCD_RST (X2.4) и CTP_RST (X2.11) объединены на
плате в `SPI0_RESET`; reset выполняет драйвер дисплея, чтобы GPIO не запрашивали
два драйвера одновременно.

Проверка после загрузки:

```sh
ls -l /dev/fb0 /dev/dri/card0 /dev/input/event*
cat /sys/class/graphics/fb0/name
cat /proc/bus/input/devices
``n
### INA238

Драйвер экспортирует стандартные hwmon-атрибуты `in0_input`, `in1_input`,
`curr1_input`, `power1_input` и `temp1_input`. Найти нужный каталог можно так:

```sh
for d in /sys/class/hwmon/hwmon*; do
    [ "$(cat "$d/name")" = ina238 ] && echo "$d"
done
```

Добавлен переключаемый во время работы атрибут `shunt_gain`:

```sh
# hwmonX замените каталогом из команды выше
echo 1 > /sys/class/hwmon/hwmonX/shunt_gain  # точный диапазон
echo 4 > /sys/class/hwmon/hwmonX/shunt_gain  # широкий диапазон
```

При R59 = 0.1 Ом режим `1` даёт диапазон примерно ±409.6 мА и шаг
напряжения шунта 1.25 мкВ; режим `4` — примерно ±1.6384 А и шаг 5 мкВ.
При загрузке выбран режим `4`. После переключения следует отбросить первый
отсчёт: он может относиться к моменту изменения масштаба.

### TPS55289

Для первичного запуска доступны простые атрибуты I2C-устройства:

```sh
cd /sys/bus/i2c/devices/*-0075
echo 5000 > output_voltage_mv  # 5.00 В
echo 500  > current_limit_ma   # 500 мА
echo 1    > forced_pwm         # 1: FPWM, 0: PFM
echo 1    > output_enable

# безопасное выключение выхода
echo 0 > output_enable
```

Допустимое напряжение — 800…20000 мВ с шагом 10 мВ. При эффективном
сопротивлении 0.05 Ом ограничение тока задаётся от 10 до 1270 мА с шагом
10 мА; значение `0`
отключает ограничение и обычно не должно применяться. Выход TPS55289
намеренно не включается автоматически. Драйвер также регистрирует обычный
regulator `multitool-vout`, чтобы управлять им из других драйверов ядра.

GPIO управления стартуют входами с pull-down и должны переводиться в выход
драйвером или приложением после выбора безопасных начальных уровней.
У AM62 режимы PRU GPO и GPI имеют разные значения pinmux. Текущий DTB
сразу задаёт IO8..IO11 входами логического анализатора PRU0, IO3 входом TDO,
а остальные линии IO0..IO7 выходами PRU1. SWDIO переключается прошивкой PRU1;
любое другое переназначение входов требует согласованного padconf в профиле.
n## Сборка

Из Ubuntu/WSL:

```sh
cd /home/dimka/PB2_multitool
./yocto/build.sh
```

Готовые файлы находятся в:

```text
yocto/build/deploy-ti/images/pocketbeagle2/
```

Для записи используйте стабильные symlink-имена:

```sh
cd yocto/build/deploy-ti/images/pocketbeagle2
sudo bmaptool copy \
  pb2-image-minimal-pocketbeagle2.wic.xz \
  /dev/sdX
sync
```

Перед запуском замените `/dev/sdX` на устройство всей microSD-карты, а не её
раздел. Команда уничтожит существующее содержимое выбранного устройства.
Если `bmaptool` недоступен:

```sh
xz -dc pb2-image-minimal-pocketbeagle2.wic.xz | \
  sudo dd of=/dev/sdX bs=4M conv=fsync status=progress
```

## Состав образа

WIC содержит MBR и два раздела:

1. FAT boot, 128 MiB: `tiboot3.bin`, `tispl.bin`, `u-boot.img`, Linux `Image`
   и `EFI/BOOT/bootaa64.efi`.
2. ext4 rootfs: минимальный BusyBox/mdev userspace.

После подачи питания плата загружается с microSD автоматически. Консоль
доступна на debug UART с параметрами 115200 8N1. Root account намеренно заблокирован; задайте пароль в локальном слое перед интерактивным входом.

## Воспроизводимость

Точные commit upstream-слоёв записаны в `source-revisions.txt`. Исходники,
downloads, sstate, build и deploy находятся только внутри `yocto/`.

## OpenOCD через PRU1

В образ включён OpenOCD с нативным адаптером `pb2_pru`. Драйвер передаёт
пакетные JTAG scan и SWD register-transfer команды в `multitoold` через
Unix-сокет `/run/multitool/openocd.sock`; PRU1 выполняет фронты, turnaround,
ACK, data/parity и idle cycles без побитовых round-trip через Linux.

Один RPMsg-пакет содержит до 400 переходов JTAG или до 32 транзакций SWD.
`adapter speed` изменяет задержку полутакта в PRU; фактическую частоту нужно
проверить осциллографом и подобрать с учётом преобразователей уровня и цели.

Распиновка по умолчанию:

| Сигнал | Канал платы | Контакт | PRU |
| --- | --- | --- | --- |
| TCK / SWCLK | IO0 | P2.08 | R30.3 |
| TMS / SWDIO | IO1 | P2.24 | R30/R31.6 |
| TDI | IO2 | P2.30 | R30.12 |
| TDO | IO3 | P2.34 | R31.14 |
| nSRST | IO4 | P2.19 | R30.16 |
| nTRST | IO5 | P2.35 | R30.8 |

TDO настроен в DT как `PRU1 GPI14`. Для SWD turnaround прошивка PRU1 сначала
изолирует 74LVC1T45, затем переключает pad P2.24 между `GPO6` и `GPI6`.
Профиль `/etc/multitool/jtag-pins.conf` задаёт биты данных и DIR, padconf
SWDIO, полярность reset и начальную задержку. Он перечитывается при каждом
подключении OpenOCD. Новый физический контакт также должен иметь подходящий
PRU pinmux в DT.

Примеры:

```sh
# SWD
openocd -f interface/pb2-multitool.cfg \
  -c "transport select swd" -f target/stm32f4x.cfg

# JTAG
openocd -f interface/pb2-multitool.cfg \
  -c "transport select jtag" -f target/<target>.cfg

# Начать с 1 МГц; повышать после проверки сигналов
openocd -f interface/pb2-multitool.cfg \
  -c "transport select swd; adapter speed 1000" \
  -f target/stm32f4x.cfg
```

## Slint UI

`multitool-ui` — лёгкое C++/Slint-приложение без Qt, Wayland и оконного
менеджера. На MSP4031 оно запускается полноэкранно через
`SLINT_BACKEND=linuxkms-software`, получает касания через libinput и управляет
`multitoold` через Unix-сокет. Уже есть три страницы: JTAG/SWD, логический
анализатор и осциллограф; кнопки запускают PRU и IIO ring capture.

Разметка и логика лежат в:

```text
yocto/meta-pb2/recipes-multitool/multitool-ui/files/appwindow.slint
yocto/meta-pb2/recipes-multitool/multitool-ui/files/main.cpp
```

Это обычный CMake-проект с `find_package(Slint)`. Для верстки на Linux,
Windows или macOS можно открыть тот же каталог в IDE и собрать его с desktop
пакетом Slint; файл `.slint` остаётся общим. Если `multitoold` недоступен
на ПК, интерфейс запускается и показывает ошибку соединения в статусной панели.