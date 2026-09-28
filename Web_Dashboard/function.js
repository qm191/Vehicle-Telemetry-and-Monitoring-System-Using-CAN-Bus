// Cấu hình Firebase GIỮ NGUYÊN VẸN 100% theo mã nguồn cũ của bạn
const firebaseConfig = {
    apiKey: "AIzaSyCa3GCepCoekoSOSNp3nT3TgbGyzpv_zqw",
    authDomain: "smart-16ec8.firebaseapp.com",
    databaseURL: "https://smart-16ec8-default-rtdb.asia-southeast1.firebasedatabase.app",
    projectId: "smart-16ec8",
    storageBucket: "smart-16ec8.firebasestorage.app",
    messagingSenderId: "657861886452",
    appId: "1:657861886452:web:a39e81359f7576b132f9a1"
};

if (!firebase.apps.length) {
    firebase.initializeApp(firebaseConfig);
}
const database = firebase.database();

let map;
let carMarker;
let charts = {};

function initCharts() {
    const commonOptions = {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
            x: { grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#a0aec0' } },
            y: { grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#a0aec0' } }
        },
        plugins: {
            legend: { labels: { color: '#ffffff', font: { size: 12 } } }
        },
        animation: { duration: 0 }
    };

    charts['perf'] = new Chart(document.getElementById('chart-performance'), {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                { label: 'Vận tốc (km/h)', data: [], borderColor: '#00ffea', backgroundColor: 'rgba(0,255,234,0.1)', fill: true, tension: 0.3, yAxisID: 'y' },
                { label: 'Vòng tua (RPM)', data: [], borderColor: '#ff9f43', backgroundColor: 'transparent', fill: false, tension: 0.3, yAxisID: 'y1' }
            ]
        },
        options: {
            ...commonOptions,
            scales: {
                ...commonOptions.scales,
                y: { ...commonOptions.scales.y, position: 'left', title: { display: true, text: 'km/h', color: '#00ffea' } },
                y1: { ...commonOptions.scales.y, position: 'right', title: { display: true, text: 'Vòng/phút (RPM)', color: '#ff9f43' }, grid: { drawOnChartArea: false } }
            }
        }
    });

    charts['env'] = new Chart(document.getElementById('chart-environment'), {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                { label: 'Nhiệt độ (°C)', data: [], borderColor: '#ff4d4d', fill: false, tension: 0.3 },
                { label: 'Độ ẩm (%)', data: [], borderColor: '#3498db', fill: false, tension: 0.3 }
            ]
        },
        options: commonOptions
    });
}

function initMap() {
    // Tự động lấy tọa độ mặc định ban đầu
    const initLat = 10.762622;
    const initLng = 106.660172;

    map = L.map('map').setView([initLat, initLng], 16);

    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        attribution: '&copy; OpenStreetMap contributors'
    }).addTo(map);

    const carIcon = L.icon({
        iconUrl: 'https://cdn-icons-png.flaticon.com/512/744/744465.png', 
        iconSize: [38, 38],
        iconAnchor: [19, 19],
        popupAnchor: [0, -15]
    });

    carMarker = L.marker([initLat, initLng], { icon: carIcon }).addTo(map)
        .bindPopup('<b>Vị trí ô tô của bạn</b>')
        .openPopup();
}

function startListeningFirebase() {
    // Trỏ chính xác vào tận nhánh 'VehicleData/Realtime' của bạn
    const vehicleRef = database.ref('VehicleData/Realtime'); 

    vehicleRef.on('value', (snapshot) => {
        const data = snapshot.val();
        if (!data) return;

        // ĐỒNG BỘ JSON: Ánh xạ chuẩn xác theo chữ cái viết hoa đầu tiên
        const velocity = data.Speed !== undefined ? data.Speed : 0;
        const rpm = data.RPM !== undefined ? data.RPM : 0;
        const temperature = data.Temperature !== undefined ? data.Temperature : 0;
        const humidity = data.Humidity !== undefined ? data.Humidity : 0;
        const energy = data.Energy !== undefined ? data.Energy : 0;
        
        // THÊM MỚI: Lấy dữ liệu Pressure từ Firebase
        const pressure = data.Pressure !== undefined ? data.Pressure : 0; 
        
        const lat = data.Lat !== undefined ? data.Lat : 10.762622;
        const lng = data.Lon !== undefined ? data.Lon : 106.660172;
        
        // A. Cập nhật số liệu Khung 1 (Thông số Xe)
        document.getElementById('val-velocity').innerText = velocity.toFixed(1);
        document.getElementById('val-rpm').innerText = rpm.toFixed(0);
        
        const errorElement = document.getElementById('val-error');
        errorElement.innerText = energy.toFixed(1) + " V";
        
        // Cảnh báo pin yếu trên giao diện Web (Dưới 12V là màu vàng/đỏ)
        if (energy > 12.0 || energy == 0) {
            errorElement.className = "sensor-value status-normal";
        } else {
            errorElement.className = "sensor-value status-warning";
        }

        // B. Cập nhật số liệu Khung 2 (Môi trường & Định vị GPS)
        document.getElementById('val-temperature').innerText = temperature.toFixed(1);
        document.getElementById('val-humidity').innerText = humidity.toFixed(1);
        document.getElementById('val-lat').innerText = lat.toFixed(6);
        document.getElementById('val-lng').innerText = lng.toFixed(6);
        
        // THÊM MỚI: In áp suất ra màn hình (làm tròn 1 chữ số thập phân, hoặc thay số 1 thành 0 nếu muốn số nguyên)
        document.getElementById('val-pressure').innerText = pressure.toFixed(1);

        // C. Điều khiển xe chạy trên OpenStreetMap theo tọa độ thực (GPS Hiện tại)
        if (carMarker && map) {
            const newLocation = new L.LatLng(lat, lng);
            carMarker.setLatLng(newLocation);
            map.panTo(newLocation); 
        }

        // D. Vẽ đồ thị thời gian thực cuộn động
        const timeNow = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
        updateChartPoints(charts['perf'], timeNow, [velocity, rpm]);
        updateChartPoints(charts['env'], timeNow, [temperature, humidity]);
    });
}

function updateChartPoints(chart, label, values) {
    if (!chart) return;
    chart.data.labels.push(label);
    
    if (Array.isArray(values)) {
        values.forEach((val, idx) => {
            if (chart.data.datasets[idx]) chart.data.datasets[idx].data.push(val);
        });
    } else {
        chart.data.datasets[0].data.push(values);
    }

    // Giữ cho đồ thị cuộn đều với 15 mốc thời gian
    if (chart.data.labels.length > 15) {
        chart.data.labels.shift();
        chart.data.datasets.forEach(dataset => dataset.data.shift());
    }
    chart.update();
}

function updateClock() {
    const now = new Date();
    const el = document.getElementById('clock');
    if (el) el.innerText = now.toLocaleTimeString('vi-VN');
}

window.onload = function() {
    initMap();
    initCharts();
    updateClock();
    setInterval(updateClock, 1000);
    startListeningFirebase();
};