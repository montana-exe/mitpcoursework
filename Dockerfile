FROM python:3.12-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ocl-icd-opencl-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --config Release -j 2

RUN pip install --no-cache-dir -r requirements.txt
ENV PYTHONPATH=/app/python
ENV ROUTEOPT_LIB=/app/build/librouteopt_core.so

EXPOSE 8000
CMD ["uvicorn", "routeopt.api:app", "--host", "0.0.0.0", "--port", "8000"]
