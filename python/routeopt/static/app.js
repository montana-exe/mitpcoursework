const routeColors = ["#087e78", "#d25b31", "#3677c9", "#8a4cb5", "#a17608", "#c23d76"];
const rowsContainer = document.getElementById("customer-rows");
const canvas = document.getElementById("route-map");
const context = canvas.getContext("2d");
let nextId = 1;
let latestResponse = null;

const backendInfo = {
  "python-fallback": ["Демонстрационный режим Python", "Нативная C++ библиотека не подключена. Ограничения учитываются, но производительный расчёт требует сборки ядра."],
  "native-cpu": ["C++ / CPU", "Маршруты рассчитаны нативным C++-ядром на центральном процессоре."],
  "native-cpu-opencl-unavailable": ["C++ / CPU", "Запрошено GPU-ускорение, но OpenCL-устройство не найдено; применён CPU."],
  "native-opencl-fitness": ["C++ / OpenCL", "Функция качества популяции рассчитана через OpenCL с учётом ограничений рейсов."]
};

function numberValue(id) {
  return Number(document.getElementById(id).value);
}

function addCustomer(values = {}) {
  const id = values.id ?? nextId++;
  nextId = Math.max(nextId, id + 1);
  const row = document.createElement("div");
  row.className = "customer-row";
  row.dataset.id = String(id);
  row.innerHTML = `
    <span class="customer-id">N${id}</span>
    <input aria-label="Координата X клиента ${id}" data-field="x" type="number" min="0" max="100" value="${values.x ?? 20}">
    <input aria-label="Координата Y клиента ${id}" data-field="y" type="number" min="0" max="100" value="${values.y ?? 20}">
    <input aria-label="Груз клиента ${id}" data-field="demand" type="number" min="0" value="${values.demand ?? 3}">
    <input aria-label="Время обслуживания клиента ${id}" data-field="service_time" type="number" min="0" value="${values.service_time ?? 4}">
    <button class="remove" type="button" title="Удалить клиента" aria-label="Удалить клиента ${id}">&times;</button>`;
  row.querySelector(".remove").addEventListener("click", () => {
    row.remove();
    drawMap(null);
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
    service_time: Number(row.querySelector('[data-field="service_time"]').value)
  }));
  return {
    depot: { id: 0, x: numberValue("depot-x"), y: numberValue("depot-y"), demand: 0, service_time: 0 },
    customers,
    vehicle: { capacity: numberValue("capacity"), max_route_time: numberValue("max-time") },
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
  button.disabled = true;
  button.textContent = "Выполняется расчёт...";
  try {
    const response = await fetch("/optimize", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    });
    if (!response.ok) {
      const error = await response.json();
      throw new Error(JSON.stringify(error.detail));
    }
    latestResponse = await response.json();
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
  document.getElementById("metric-distance").textContent = `${result.total_distance.toFixed(2)} ед.`;
  document.getElementById("metric-duration").textContent = `${result.total_duration.toFixed(2)} ед.`;
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
        <strong>${route.distance.toFixed(2)} ед.</strong>
      </div>
      <div class="route-details">Депо -> ${clientText} -> Депо<br>
      Груз: ${route.load.toFixed(1)} из ${body.vehicle.capacity.toFixed(1)} ед. · Время: ${route.duration.toFixed(2)} ед.</div>
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

function showHelp(type) {
  const help = document.getElementById("help-content");
  if (type === "algorithm") {
    help.innerHTML = `
      <dt>Популяция</dt><dd>Количество вариантов маршрута, сравниваемых в одном поколении.</dd>
      <dt>Поколения</dt><dd>Число циклов отбора, скрещивания и мутации решений.</dd>
      <dt>Seed</dt><dd>Число для повторяемости эксперимента с теми же данными.</dd>
      <dt>GPU / OpenCL</dt><dd>Запрос ускоренной оценки вариантов маршрута при собранном C++-ядре и доступном устройстве.</dd>`;
  } else {
    help.innerHTML = `
      <dt>Грузоподъёмность</dt><dd>Предельная сумма груза клиентов в одном рейсе.</dd>
      <dt>Макс. время рейса</dt><dd>Предел пути и обслуживания клиентов до возврата в депо; 0 отключает предел.</dd>
      <dt>Обсл.</dt><dd>Время передачи заказа одному клиенту.</dd>
      <dt>Депо</dt><dd>Начальная и конечная точка каждого рейса.</dd>`;
  }
}

document.getElementById("add-customer").addEventListener("click", () => addCustomer());
document.getElementById("generate").addEventListener("click", generateCustomers);
document.getElementById("optimize").addEventListener("click", optimize);
document.querySelectorAll(".info-btn").forEach((button) => {
  button.addEventListener("click", () => showHelp(button.dataset.help));
});
window.addEventListener("resize", resizeCanvas);

setSampleCustomers();
showHelp("conditions");
checkSystem();
requestAnimationFrame(resizeCanvas);
optimize();
