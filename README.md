# RouteOpt: генетическая оптимизация маршрутов доставки

RouteOpt — курсовой проект по дисциплине «Методы и технологии программирования». Система решает прикладную задачу маршрутизации доставок с ограничениями по грузоподъёмности и длительности маршрута. Вариант из методических указаний: **22. Генетический алгоритм оптимизации маршрутов доставки**, предметная область — логистика, стек — **C++ + Python + OpenCL**.

## Архитектурные решения

1. **C++20 выбран для алгоритмического ядра.** Расчёт приспособленности, декодирование хромосомы и генетические операторы находятся в `cpp/`, поскольку эти операции определяют производительность системы.
2. **Python используется как прикладной слой.** FastAPI предоставляет HTTP-интерфейс, Pydantic проверяет входные данные, Matplotlib строит карту маршрутов для экспериментов и отчётов.
3. **OpenCL выделен отдельным контуром.** Ядро `cpp/kernels/fitness.cl` предназначено для параллельной оценки расстояний популяции. Если OpenCL недоступен, система остаётся работоспособной на CPU; это важно для CI и обычных учебных машин.
4. **Связь Python и C++ выполнена через C API и `ctypes`.** Такой вариант проще для контейнерной сборки, чем Python-расширение на pybind11, и не привязывает проект к конкретной версии CPython.
5. **Ограничения маршрута учитываются при декодировании хромосомы.** Хромосома задаёт порядок клиентов, а разбиение на рейсы выполняется с учётом грузоподъёмности и максимального времени маршрута.
6. **Тестирование разделено по уровням.** C++-тест проверяет корректность ядра, Python-тесты проверяют валидацию, API и интеграционное поведение оптимизатора.
7. **Бенчмарк CPU/GPU вынесен в `benchmarks/`.** Скрипт запускает CLI в двух режимах и сохраняет JSON-результаты; при отсутствии OpenCL GPU-режим явно отражает CPU-оценку с запрошенным OpenCL-контуром.

## Структура

```text
cpp/                 C++ ядро, CLI, тесты и OpenCL kernel
python/routeopt/     Python API, модели, визуализация, ctypes-интеграция
python/tests/        pytest-тесты Python-слоя
benchmarks/          сравнение CPU и GPU-контуров
docs/course-paper/   пояснительная записка курсовой работы
docs/diagrams/       диаграммы Mermaid
scripts/             генерация экспериментальных сценариев
```

## Локальный запуск Python-слоя

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements-dev.txt
PYTHONPATH=python pytest
PYTHONPATH=python uvicorn routeopt.api:app --reload
```

Если C++ библиотека не собрана, Python-слой использует ограниченный fallback. Для итогового запуска следует использовать Docker или CMake-сборку.

## Сборка C++ и тесты

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

После сборки Python можно связать с C++ ядром:

```bash
export PYTHONPATH=python
export ROUTEOPT_LIB=$PWD/build/librouteopt_core.so
pytest
```

## Docker

```bash
docker compose up --build
```

API будет доступно по адресу `http://localhost:8000`. Проверка:

```bash
curl http://localhost:8000/health
```

## Бенчмарк CPU/GPU

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 2
python benchmarks/benchmark_cpu_gpu.py --cli build/routeopt_cli --customers 250 --generations 300
```

Результат сохраняется в `benchmarks/results.json`. Поля `requested_backend` и `backend` позволяют отличать запрошенный GPU-режим от фактически доступного исполнения.

## API

`POST /optimize` принимает координаты депо, список клиентов, параметры автомобиля и настройки генетического алгоритма.

Минимальный пример тела запроса:

```json
{
  "depot": {"id": 0, "x": 0, "y": 0, "demand": 0},
  "customers": [
    {"id": 1, "x": 1, "y": 0, "demand": 1},
    {"id": 2, "x": 0, "y": 1, "demand": 1}
  ],
  "vehicle": {"capacity": 2, "max_route_time": 0},
  "settings": {"population_size": 96, "generations": 250, "seed": 42}
}
```

## Git flow и семантические коммиты

Для учебного проекта используется упрощённый Git flow:

- `main` — стабильное состояние;
- `feature/core-ga` — развитие C++ ядра;
- `feature/python-api` — развитие API и визуализации;
- `docs/course-paper` — оформление пояснительной записки.

Коммиты оформляются по Conventional Commits: `feat:`, `test:`, `docs:`, `build:`, `ci:`.
