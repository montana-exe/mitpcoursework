const routeColors = ["#087e78", "#d25b31", "#3677c9", "#8a4cb5", "#a17608", "#c23d76"];
const rowsContainer = document.getElementById("customer-rows");
const canvas = document.getElementById("route-map");
const context = canvas.getContext("2d");
let nextId = 1;
let latestResponse = null;
const isStaticDemo = window.location.hostname.endsWith("github.io") ||
  new URLSearchParams(window.location.search).has("demo");

const backendInfo = {
  "python-fallback": ["Демонстрационный режим Python", "Нативная C++ библиотека не подключена. Ограничения учитываются, но производительный расчёт требует сборки ядра."],
  "browser-demo": ["Демо на GitHub Pages", "Расчёт выполняется в браузере для показа интерфейса. Полный генетический алгоритм запускается локально через API."],
  "native-cpu": ["C++ / CPU", "Маршруты рассчитаны нативным C++-ядром на центральном процессоре."],
  "native-cpu-opencl-unavailable": ["C++ / CPU", "Запрошено GPU-ускорение, но OpenCL-устройство не найдено; применён CPU."],
  "native-opencl-fitness": ["C++ / OpenCL", "Функция качества популяции рассчитана через OpenCL с учётом ограничений рейсов."]
};

function numberValue(id) {
  return Number(document.getElementById(id).value);
}

function clampCoordinate(input) {
  const value = Number(input.value);
  if (!Number.isFinite(value)) return;
  input.value = String(Math.max(0, Math.min(100, value)));
}

function addCustomer(values = {}) {
  const id = values.id ?? nextId++;
  nextId = Math.max(nextId, id + 1);
  const row = document.createElement("div");
  row.className = "customer-row";
  row.dataset.id = String(id);
  row.innerHTML = `
    <span class="customer-id">N${id}</span>
    <input class="coordinate" aria-label="Координата X клиента ${id}" data-field="x" type="number" min="0" max="100" value="${values.x ?? 20}">
    <input class="coordinate" aria-label="Координата Y клиента ${id}" data-field="y" type="number" min="0" max="100" value="${values.y ?? 20}">
    <input aria-label="Груз клиента ${id}" data-field="demand" type="number" min="0" value="${values.demand ?? 3}">
    <input aria-label="Время обслуживания клиента ${id}" data-field="service_time" type="number" min="0" value="${values.service_time ?? 4}">
    <button class="remove" type="button" title="Удалить клиента" aria-label="Удалить клиента ${id}">&times;</button>`;
  row.querySelector(".remove").addEventListener("click", () => {
    row.remove();
    drawMap(null);
  });
  row.querySelectorAll(".coordinate").forEach((input) => {
    input.addEventListener("change", () => clampCoordinate(input));
  });
  rowsContainer.appendChild(row);
}

function setSampleCustomers() {
  rowsContainer.innerHTML = "";
  nextId = 1;
  [
    { x: 14, y: 22, demand: 5, service_time: 5 },
    { x: 24, y: 76, demand: 4, service_time: 4 },
    { x: 39, y: 15, demand: 6, service_time: 6 },
    { x: 67, y: 23, demand: 3, service_time: 4 },
    { x: 82, y: 57, demand: 5, service_time: 5 },
    { x: 69, y: 84, demand: 4, service_time: 6 },
    { x: 34, y: 62, demand: 2, service_time: 3 },
    { x: 87, y: 14, demand: 3, service_time: 3 }
  ].forEach(addCustomer);
  drawMap(null);
}

function generateCustomers() {
  rowsContainer.innerHTML = "";
  nextId = 1;
  for (let index = 0; index < 12; index += 1) {
    addCustomer({
      x: 8 + Math.floor(Math.random() * 84),
      y: 8 + Math.floor(Math.random() * 84),
      demand: 1 + Math.floor(Math.random() * 6),
      service_time: 2 + Math.floor(Math.random() * 7)
    });
  }
  drawMap(null);
}

function requestBody() {
  const customers = [...rowsContainer.querySelectorAll(".customer-row")].map((row) => ({
    id: Number(row.dataset.id),
    x: Number(row.querySelector('[data-field="x"]').value),
    y: Number(row.querySelector('[data-field="y"]').value),
    demand: Number(row.querySelector('[data-field="demand"]').value),
    service_time: Number(row.querySelector('[data-field="service_time"]').value) / 60
  }));
  return {
    depot: { id: 0, x: numberValue("depot-x"), y: numberValue("depot-y"), demand: 0, service_time: 0 },
    customers,
    vehicle: {
      capacity: numberValue("capacity"),
      max_route_time: numberValue("max-time"),
      average_speed_kmh: numberValue("speed")
    },
    settings: {
      population_size: numberValue("population"),
      generations: numberValue("generations"),
      seed: numberValue("seed"),
      prefer_gpu: document.getElementById("gpu").checked
    }
  };
}

async function checkSystem() {
  const indicator = document.getElementById("system-indicator");
  const title = document.getElementById("system-title");
  const detail = document.getElementById("system-detail");
  if (isStaticDemo) {
    indicator.classList.add("ready");
    title.textContent = "Онлайн-демо";
    detail.textContent = "Расчёт выполняется в браузере";
    return;
  }
  try {
    const response = await fetch("/health");
    const data = await response.json();
    indicator.classList.add("ready");
    title.textContent = "API работает";
    detail.textContent = data.native_library === "loaded" ? "C++ ядро подключено" : "Демонстрационный Python-режим";
  } catch (error) {
    title.textContent = "API недоступно";
    detail.textContent = "Перезапустите сервер";
  }
}

async function optimize() {
  const button = document.getElementById("optimize");
  const body = requestBody();
  if (body.customers.length === 0) {
    window.alert("Добавьте хотя бы одну точку доставки.");
    return;
  }
  const invalidNode = [body.depot, ...body.customers].find((node) => (
    node.x < 0 || node.x > 100 || node.y < 0 || node.y > 100
  ));
  if (invalidNode) {
    window.alert("Координаты должны находиться внутри карты: от 0 до 100 км по X и Y.");
    return;
  }
  button.disabled = true;
  button.textContent = "Выполняется расчёт...";
  try {
    if (isStaticDemo) {
      latestResponse = browserDemoOptimize(body);
    } else {
      const response = await fetch("./optimize", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body)
      });
      if (!response.ok) {
        const error = await response.json();
        throw new Error(JSON.stringify(error.detail));
      }
      latestResponse = await response.json();
    }
    renderResults(body, latestResponse);
    drawMap(latestResponse, body);
  } catch (error) {
    window.alert(`Не удалось выполнить расчёт: ${error.message}`);
  } finally {
    button.disabled = false;
    button.textContent = "Рассчитать маршруты";
  }
}

function renderResults(body, result) {
  document.getElementById("result-placeholder").classList.add("hidden");
  document.getElementById("metrics").classList.remove("hidden");
  document.getElementById("metric-routes").textContent = String(result.routes.length);
  document.getElementById("metric-distance").textContent = `${result.total_distance.toFixed(2)} км`;
  document.getElementById("metric-duration").textContent = `${result.total_duration.toFixed(2)} ч`;
  const feasible = document.getElementById("metric-feasible");
  feasible.textContent = result.feasible ? "соблюдены" : "нарушены";
  feasible.className = result.feasible ? "good" : "bad";

  const backend = document.getElementById("backend");
  const mode = backendInfo[result.backend] ?? [result.backend, "Режим передан вычислительным модулем."];
  backend.classList.remove("hidden");
  backend.innerHTML = `<strong>${mode[0]}</strong><br>${mode[1]}`;

  const list = document.getElementById("route-list");
  list.className = "route-list";
  list.innerHTML = "";
  result.routes.forEach((route, index) => {
    const item = document.createElement("div");
    item.className = "route-item";
    const color = routeColors[index % routeColors.length];
    const clientText = route.customer_ids.map((id) => `N${id}`).join(" -> ");
    item.innerHTML = `
      <div class="route-title">
        <span class="route-name"><span class="dot" style="background:${color}"></span>Рейс ${index + 1}</span>
        <strong>${route.distance.toFixed(2)} км</strong>
      </div>
      <div class="route-details">Депо -> ${clientText} -> Депо<br>
      Груз: ${route.load.toFixed(1)} из ${body.vehicle.capacity.toFixed(1)} кг · Время: ${route.duration.toFixed(2)} ч</div>
      <div class="bar"><div class="bar-fill" style="width:${Math.min(route.capacity_utilization, 100)}%;background:${color}"></div></div>`;
    list.appendChild(item);
  });

  document.getElementById("json-output").textContent = JSON.stringify(result, null, 2);
}

function resizeCanvas() {
  const ratio = window.devicePixelRatio || 1;
  const box = canvas.getBoundingClientRect();
  canvas.width = Math.floor(box.width * ratio);
  canvas.height = Math.floor(box.height * ratio);
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  drawMap(latestResponse, requestBody());
}

function drawMap(result, body = requestBody()) {
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  const padding = 38;
  context.clearRect(0, 0, width, height);
  context.strokeStyle = "#e9edef";
  context.lineWidth = 1;
  for (let value = 0; value <= 100; value += 10) {
    const x = padding + (value / 100) * (width - padding * 2);
    const y = height - padding - (value / 100) * (height - padding * 2);
    context.beginPath();
    context.moveTo(x, padding);
    context.lineTo(x, height - padding);
    context.stroke();
    context.beginPath();
    context.moveTo(padding, y);
    context.lineTo(width - padding, y);
    context.stroke();
  }
  context.fillStyle = "#839198";
  context.font = "11px Segoe UI";
  context.fillText("0 км", padding - 4, height - padding + 18);
  context.fillText("100 км", width - padding - 32, height - padding + 18);
  context.fillText("100 км", padding - 32, padding + 4);

  const toPoint = (node) => ({
    x: padding + (node.x / 100) * (width - padding * 2),
    y: height - padding - (node.y / 100) * (height - padding * 2)
  });
  const byId = new Map(body.customers.map((customer) => [customer.id, customer]));
  const depot = toPoint(body.depot);

  if (result) {
    result.routes.forEach((route, index) => {
      const points = [body.depot, ...route.customer_ids.map((id) => byId.get(id)), body.depot].map(toPoint);
      context.strokeStyle = routeColors[index % routeColors.length];
      context.lineWidth = 2.5;
      context.beginPath();
      points.forEach((point, pointIndex) => {
        if (pointIndex === 0) context.moveTo(point.x, point.y);
        else context.lineTo(point.x, point.y);
      });
      context.stroke();
    });
  }

  body.customers.forEach((customer) => {
    const point = toPoint(customer);
    context.fillStyle = "#ffffff";
    context.strokeStyle = "#52646d";
    context.lineWidth = 2;
    context.beginPath();
    context.arc(point.x, point.y, 7, 0, Math.PI * 2);
    context.fill();
    context.stroke();
    context.fillStyle = "#17252d";
    context.font = "12px Segoe UI";
    context.fillText(`N${customer.id}`, point.x + 10, point.y - 8);
  });

  context.fillStyle = "#17252d";
  context.fillRect(depot.x - 8, depot.y - 8, 16, 16);
  context.font = "12px Segoe UI";
  context.fillText("Депо", depot.x + 12, depot.y - 9);
  document.getElementById("map-empty").classList.toggle("hidden", body.customers.length > 0);
  updateLegend(result);
}

function updateLegend(result) {
  const legend = document.getElementById("legend");
  legend.innerHTML = '<span class="legend-item"><span class="dot depot"></span>Депо</span>';
  if (!result) return;
  result.routes.forEach((_, index) => {
    const line = document.createElement("span");
    line.className = "legend-item";
    line.innerHTML = `<span class="legend-line" style="background:${routeColors[index % routeColors.length]}"></span>Рейс ${index + 1}`;
    legend.appendChild(line);
  });
}

function distance(lhs, rhs) {
  return Math.hypot(lhs.x - rhs.x, lhs.y - rhs.y);
}

function routeMetrics(body, customerIds) {
  const customers = new Map(body.customers.map((customer) => [customer.id, customer]));
  const points = [body.depot, ...customerIds.map((id) => customers.get(id)), body.depot];
  let routeDistance = 0;
  for (let index = 0; index < points.length - 1; index += 1) {
    routeDistance += distance(points[index], points[index + 1]);
  }
  const load = customerIds.reduce((sum, id) => sum + customers.get(id).demand, 0);
  const serviceTime = customerIds.reduce((sum, id) => sum + customers.get(id).service_time, 0);
  return {
    customer_ids: customerIds,
    load,
    distance: routeDistance,
    duration: routeDistance / body.vehicle.average_speed_kmh + serviceTime,
    capacity_utilization: load / body.vehicle.capacity * 100
  };
}

function browserDemoOptimize(body) {
  const ordered = [...body.customers].sort((a, b) => a.x - b.x || a.y - b.y);
  const routes = [];
  let current = [];
  ordered.forEach((customer) => {
    const candidate = routeMetrics(body, [...current, customer.id]);
    const exceeds = candidate.load > body.vehicle.capacity ||
      (body.vehicle.max_route_time > 0 && candidate.duration > body.vehicle.max_route_time);
    if (current.length && exceeds) {
      routes.push(routeMetrics(body, current));
      current = [];
    }
    current.push(customer.id);
  });
  if (current.length) routes.push(routeMetrics(body, current));
  const feasible = routes.every((route) => (
    route.load <= body.vehicle.capacity &&
    (body.vehicle.max_route_time === 0 || route.duration <= body.vehicle.max_route_time)
  ));
  return {
    routes,
    total_distance: routes.reduce((sum, route) => sum + route.distance, 0),
    total_duration: routes.reduce((sum, route) => sum + route.duration, 0),
    feasible,
    backend: "browser-demo"
  };
}

document.getElementById("add-customer").addEventListener("click", () => addCustomer());
document.getElementById("generate").addEventListener("click", generateCustomers);
document.getElementById("optimize").addEventListener("click", optimize);
document.querySelectorAll(".coordinate").forEach((input) => {
  input.addEventListener("change", () => clampCoordinate(input));
});
document.querySelectorAll(".info-btn").forEach((button) => {
  const tooltip = document.getElementById(button.dataset.tooltip);
  button.addEventListener("mouseenter", () => tooltip.classList.add("open"));
  button.addEventListener("mouseleave", () => tooltip.classList.remove("open"));
  button.addEventListener("focus", () => tooltip.classList.add("open"));
  button.addEventListener("blur", () => tooltip.classList.remove("open"));
  button.addEventListener("click", () => tooltip.classList.toggle("open"));
});
document.addEventListener("click", (event) => {
  if (!event.target.closest(".contextual-panel")) {
    document.querySelectorAll(".tooltip").forEach((tooltip) => tooltip.classList.remove("open"));
  }
});
window.addEventListener("resize", resizeCanvas);

setSampleCustomers();
checkSystem();
requestAnimationFrame(resizeCanvas);
optimize();
