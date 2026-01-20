/**
 * TianShanOS Web App - Main Application
 */

// =========================================================================
//                         全局状态
// =========================================================================

let ws = null;
let refreshInterval = null;

// =========================================================================
//                         初始化
// =========================================================================

document.addEventListener('DOMContentLoaded', () => {
    // 初始化认证 UI
    updateAuthUI();
    
    // 注册路由
    router.register('/', loadDashboard);
    router.register('/system', loadSystemPage);
    router.register('/led', loadLedPage);
    router.register('/network', loadNetworkPage);
    router.register('/device', loadDevicePage);
    router.register('/terminal', loadTerminalPage);
    router.register('/config', loadConfigPage);
    router.register('/security', loadSecurityPage);
    
    // 启动 WebSocket
    setupWebSocket();
});

// =========================================================================
//                         认证
// =========================================================================

function updateAuthUI() {
    const loginBtn = document.getElementById('login-btn');
    const userName = document.getElementById('user-name');
    
    if (api.isLoggedIn()) {
        loginBtn.textContent = '登出';
        userName.textContent = '已登录';
        loginBtn.onclick = logout;
    } else {
        loginBtn.textContent = '登录';
        userName.textContent = '未登录';
        loginBtn.onclick = showLoginModal;
    }
}

function showLoginModal() {
    document.getElementById('login-modal').classList.remove('hidden');
}

function closeLoginModal() {
    document.getElementById('login-modal').classList.add('hidden');
    document.getElementById('login-form').reset();
}

document.getElementById('login-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    
    try {
        await api.login(username, password);
        closeLoginModal();
        updateAuthUI();
        router.navigate();
    } catch (error) {
        showToast('登录失败: ' + error.message, 'error');
    }
});

async function logout() {
    try {
        await api.logout();
    } finally {
        updateAuthUI();
    }
}

// =========================================================================
//                         WebSocket
// =========================================================================

function setupWebSocket() {
    ws = new TianShanWS(
        (msg) => handleEvent(msg),
        () => document.getElementById('ws-status')?.classList.add('connected'),
        () => document.getElementById('ws-status')?.classList.remove('connected')
    );
    ws.connect();
}

function handleEvent(msg) {
    console.log('Event:', msg);
    
    if (msg.type === 'event') {
        // 刷新相关页面数据
        if (router.currentPage) {
            router.currentPage();
        }
    }
}

// =========================================================================
//                         仪表盘页面
// =========================================================================

async function loadDashboard() {
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="dashboard">
            <h1>仪表盘</h1>
            
            <div class="cards">
                <div class="card">
                    <h3>🖥️ 系统信息</h3>
                    <div class="card-content" id="sys-info-card">
                        <p><strong>芯片:</strong> <span id="chip-model">-</span></p>
                        <p><strong>固件:</strong> <span id="firmware-version">-</span></p>
                        <p><strong>运行时间:</strong> <span id="uptime">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>💾 内存</h3>
                    <div class="card-content">
                        <div class="progress-bar">
                            <div class="progress" id="mem-progress" style="width: 0%"></div>
                        </div>
                        <p><span id="mem-used">-</span> / <span id="mem-total">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🌐 网络</h3>
                    <div class="card-content">
                        <p><strong>以太网:</strong> <span id="eth-status">-</span></p>
                        <p><strong>WiFi:</strong> <span id="wifi-status">-</span></p>
                        <p><strong>IP:</strong> <span id="ip-addr">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>⚡ 电源</h3>
                    <div class="card-content">
                        <p><strong>电压:</strong> <span id="voltage">-</span></p>
                        <p><strong>保护:</strong> <span id="protection-status">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🖲️ 设备</h3>
                    <div class="card-content">
                        <p><strong>AGX:</strong> <span id="agx-status">-</span></p>
                        <p><strong>LPMU:</strong> <span id="lpmu-status">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🌡️ 温度 & 风扇</h3>
                    <div class="card-content">
                        <p><strong>温度:</strong> <span id="temperature">-</span></p>
                        <p><strong>风扇:</strong> <span id="fan-status">-</span></p>
                    </div>
                </div>
            </div>
        </div>
    `;
    
    await refreshDashboard();
    
    // 定时刷新
    clearInterval(refreshInterval);
    refreshInterval = setInterval(refreshDashboard, 3000);
}

async function refreshDashboard() {
    // 系统信息
    try {
        const sysInfo = await api.getSystemInfo();
        if (sysInfo.data) {
            document.getElementById('chip-model').textContent = sysInfo.data.chip?.model || '-';
            document.getElementById('firmware-version').textContent = sysInfo.data.app?.version || '-';
            document.getElementById('uptime').textContent = formatUptime(sysInfo.data.uptime_ms);
        }
    } catch (e) { console.log('System info not available'); }
    
    // 内存
    try {
        const memInfo = await api.getMemoryInfo();
        if (memInfo.data) {
            const total = memInfo.data.internal?.total || 1;
            const free = memInfo.data.internal?.free || memInfo.data.free_heap || 0;
            const used = total - free;
            const percent = Math.round((used / total) * 100);
            
            document.getElementById('mem-progress').style.width = percent + '%';
            document.getElementById('mem-used').textContent = formatBytes(used);
            document.getElementById('mem-total').textContent = formatBytes(total);
        }
    } catch (e) { console.log('Memory info not available'); }
    
    // 网络
    try {
        const netStatus = await api.networkStatus();
        if (netStatus.data) {
            const eth = netStatus.data.ethernet || {};
            const wifi = netStatus.data.wifi || {};
            document.getElementById('eth-status').textContent = eth.status === 'connected' ? '已连接' : '未连接';
            document.getElementById('wifi-status').textContent = wifi.connected ? '已连接' : '未连接';
            document.getElementById('ip-addr').textContent = eth.ip || wifi.ip || '-';
        }
    } catch (e) {
        document.getElementById('eth-status').textContent = '-';
        document.getElementById('wifi-status').textContent = '-';
    }
    
    // 电源
    try {
        const powerStatus = await api.powerStatus();
        if (powerStatus.data) {
            // 优先使用 power_chip 数据，其次用 voltage 数据
            const voltage = powerStatus.data.power_chip?.voltage_v || 
                           powerStatus.data.voltage?.supply_v || 
                           powerStatus.data.stats?.avg_voltage_v || '-';
            document.getElementById('voltage').textContent = 
                (typeof voltage === 'number' ? voltage.toFixed(1) : voltage) + ' V';
        }
        const protStatus = await api.powerProtectionStatus();
        if (protStatus.data) {
            const running = protStatus.data.running || protStatus.data.initialized;
            document.getElementById('protection-status').textContent = 
                running ? '已启用' : '已禁用';
        }
    } catch (e) { document.getElementById('voltage').textContent = '-'; }
    
    // 设备
    try {
        const devStatus = await api.deviceStatus();
        if (devStatus.data) {
            const agx = devStatus.data.devices?.find(d => d.name === 'agx');
            const lpmu = devStatus.data.devices?.find(d => d.name === 'lpmu');
            document.getElementById('agx-status').textContent = agx?.powered ? '运行中' : '关机';
            document.getElementById('lpmu-status').textContent = lpmu?.powered ? '运行中' : '关机';
        }
    } catch (e) {
        document.getElementById('agx-status').textContent = '-';
        document.getElementById('lpmu-status').textContent = '-';
    }
    
    // 温度和风扇
    try {
        const tempStatus = await api.tempStatus();
        if (tempStatus.data) {
            document.getElementById('temperature').textContent = 
                (tempStatus.data.temperature || '-') + ' °C';
        }
        const fanStatus = await api.fanStatus();
        if (fanStatus.data) {
            const fans = fanStatus.data.fans || [];
            const running = fans.filter(f => f.enabled).length;
            document.getElementById('fan-status').textContent = `${running}/${fans.length} 运行`;
        }
    } catch (e) {
        document.getElementById('temperature').textContent = '-';
        document.getElementById('fan-status').textContent = '-';
    }
}

// =========================================================================
//                         系统页面
// =========================================================================

async function loadSystemPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-system">
            <h1>系统管理</h1>
            
            <div class="section">
                <h2>系统信息</h2>
                <div class="info-grid" id="system-info">
                    <div class="info-item"><label>芯片</label><span id="sys-chip">-</span></div>
                    <div class="info-item"><label>版本</label><span id="sys-version">-</span></div>
                    <div class="info-item"><label>编译时间</label><span id="sys-compile">-</span></div>
                    <div class="info-item"><label>运行时间</label><span id="sys-uptime">-</span></div>
                    <div class="info-item"><label>IDF版本</label><span id="sys-idf">-</span></div>
                    <div class="info-item"><label>Flash大小</label><span id="sys-flash">-</span></div>
                </div>
            </div>
            
            <div class="section">
                <h2>内存状态</h2>
                <div class="memory-bars">
                    <div class="memory-item">
                        <label>总堆内存</label>
                        <div class="progress-bar"><div class="progress" id="heap-progress"></div></div>
                        <span id="heap-text">-</span>
                    </div>
                    <div class="memory-item">
                        <label>PSRAM</label>
                        <div class="progress-bar"><div class="progress" id="psram-progress"></div></div>
                        <span id="psram-text">-</span>
                    </div>
                </div>
            </div>
            
            <div class="section">
                <h2>服务状态</h2>
                <table class="data-table" id="services-table">
                    <thead>
                        <tr>
                            <th>服务名称</th>
                            <th>状态</th>
                            <th>阶段</th>
                            <th>健康</th>
                            <th>操作</th>
                        </tr>
                    </thead>
                    <tbody id="services-body"></tbody>
                </table>
            </div>
            
            <div class="section">
                <h2>系统操作</h2>
                <div class="button-group">
                    <button class="btn btn-warning" onclick="confirmReboot()">🔄 重启系统</button>
                </div>
            </div>
        </div>
    `;
    
    await refreshSystemPage();
}

async function refreshSystemPage() {
    // 系统信息
    try {
        const info = await api.getSystemInfo();
        if (info.data) {
            document.getElementById('sys-chip').textContent = info.data.chip?.model || '-';
            document.getElementById('sys-version').textContent = info.data.app?.version || '-';
            document.getElementById('sys-compile').textContent = 
                (info.data.app?.compile_date || '') + ' ' + (info.data.app?.compile_time || '');
            document.getElementById('sys-uptime').textContent = formatUptime(info.data.uptime_ms);
            document.getElementById('sys-idf').textContent = info.data.app?.idf_version || '-';
            document.getElementById('sys-flash').textContent = formatBytes(info.data.flash_size || 0);
        }
    } catch (e) { console.log('System info error:', e); }
    
    // 内存
    try {
        const mem = await api.getMemoryInfo();
        if (mem.data) {
            const heapTotal = mem.data.internal?.total || 1;
            const heapFree = mem.data.internal?.free || mem.data.free_heap || 0;
            const heapUsed = heapTotal - heapFree;
            const heapPercent = Math.round((heapUsed / heapTotal) * 100);
            
            document.getElementById('heap-progress').style.width = heapPercent + '%';
            document.getElementById('heap-text').textContent = 
                `${formatBytes(heapUsed)} / ${formatBytes(heapTotal)} (${heapPercent}%)`;
            
            if (mem.data.psram?.total) {
                const psramTotal = mem.data.psram.total;
                const psramFree = mem.data.psram.free || 0;
                const psramUsed = psramTotal - psramFree;
                const psramPercent = Math.round((psramUsed / psramTotal) * 100);
                
                document.getElementById('psram-progress').style.width = psramPercent + '%';
                document.getElementById('psram-text').textContent = 
                    `${formatBytes(psramUsed)} / ${formatBytes(psramTotal)} (${psramPercent}%)`;
            }
        }
    } catch (e) { console.log('Memory info error:', e); }
    
    // 服务列表
    try {
        const services = await api.serviceList();
        const tbody = document.getElementById('services-body');
        tbody.innerHTML = '';
        
        if (services.data && services.data.services) {
            services.data.services.forEach(svc => {
                const tr = document.createElement('tr');
                const stateClass = svc.state === 'RUNNING' ? 'status-ok' : 
                                  svc.state === 'ERROR' ? 'status-error' : 'status-warn';
                tr.innerHTML = `
                    <td>${svc.name}</td>
                    <td><span class="status-badge ${stateClass}">${svc.state}</span></td>
                    <td>${svc.phase}</td>
                    <td>${svc.healthy ? '✅' : '❌'}</td>
                    <td>
                        <button class="btn btn-small" onclick="serviceAction('${svc.name}', 'restart')">重启</button>
                    </td>
                `;
                tbody.appendChild(tr);
            });
        }
    } catch (e) { console.log('Services error:', e); }
}

async function serviceAction(name, action) {
    try {
        if (action === 'restart') await api.serviceRestart(name);
        else if (action === 'start') await api.serviceStart(name);
        else if (action === 'stop') await api.serviceStop(name);
        showToast(`服务 ${name} ${action} 成功`, 'success');
        await refreshSystemPage();
    } catch (e) {
        showToast(`操作失败: ${e.message}`, 'error');
    }
}

function confirmReboot() {
    if (confirm('确定要重启系统吗？')) {
        api.reboot().then(() => showToast('系统正在重启...', 'info'));
    }
}

// =========================================================================
//                         LED 页面
// =========================================================================

async function loadLedPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-led">
            <h1>LED 控制</h1>
            
            <div class="led-devices" id="led-devices"></div>
            
            <div class="section">
                <h2>特效列表</h2>
                <div class="effects-grid" id="effects-list"></div>
            </div>
            
            <div class="section">
                <h2>颜色选择</h2>
                <div class="color-picker">
                    <input type="color" id="led-color" value="#ff0000">
                    <button class="btn btn-primary" onclick="applyColor()">应用颜色</button>
                </div>
            </div>
        </div>
    `;
    
    await refreshLedPage();
}

async function refreshLedPage() {
    const devicesContainer = document.getElementById('led-devices');
    
    try {
        const result = await api.ledList();
        if (result.data && result.data.devices) {
            devicesContainer.innerHTML = result.data.devices.map(dev => `
                <div class="card led-device-card">
                    <h3>💡 ${dev.name}</h3>
                    <div class="card-content">
                        <p><strong>LED 数量:</strong> ${dev.count}</p>
                        <p><strong>亮度:</strong> ${dev.brightness}</p>
                        <div class="control-row">
                            <label>亮度调节</label>
                            <input type="range" min="0" max="255" value="${dev.brightness}" 
                                   onchange="setBrightness('${dev.name}', this.value)">
                        </div>
                        <div class="button-group">
                            <button class="btn" onclick="clearLed('${dev.name}')">清除</button>
                        </div>
                    </div>
                </div>
            `).join('');
        }
    } catch (e) {
        devicesContainer.innerHTML = '<p class="error">无法加载 LED 设备</p>';
    }
    
    // 加载特效列表
    try {
        const effects = await api.ledEffectList();
        const effectsContainer = document.getElementById('effects-list');
        if (effects.data && effects.data.effects) {
            effectsContainer.innerHTML = effects.data.effects.map(eff => `
                <button class="btn effect-btn" onclick="startEffect('${eff.name}')">
                    ${eff.name}
                </button>
            `).join('');
        }
    } catch (e) { console.log('Effects error:', e); }
}

async function setBrightness(device, value) {
    try {
        await api.ledBrightness(device, parseInt(value));
    } catch (e) { showToast('设置亮度失败', 'error'); }
}

async function clearLed(device) {
    try {
        await api.ledClear(device);
        showToast('LED 已清除', 'success');
    } catch (e) { showToast('清除失败', 'error'); }
}

async function startEffect(effect) {
    // 获取当前选中设备
    const device = document.querySelector('.led-device-card h3')?.textContent.replace('💡 ', '').trim();
    if (!device) return;
    
    try {
        await api.ledEffectStart(device, effect);
        showToast(`特效 ${effect} 已启动`, 'success');
    } catch (e) { showToast('启动特效失败', 'error'); }
}

async function applyColor() {
    const color = document.getElementById('led-color').value;
    const device = document.querySelector('.led-device-card h3')?.textContent.replace('💡 ', '').trim();
    if (!device) return;
    
    try {
        await api.ledFill(device, color);
        showToast('颜色已应用', 'success');
    } catch (e) { showToast('应用颜色失败', 'error'); }
}

// =========================================================================
//                         网络页面
// =========================================================================

async function loadNetworkPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-network">
            <h1>网络配置</h1>
            
            <div class="cards">
                <div class="card">
                    <h3>🔌 以太网</h3>
                    <div class="card-content" id="eth-info">
                        <p><strong>状态:</strong> <span id="net-eth-status">-</span></p>
                        <p><strong>IP:</strong> <span id="net-eth-ip">-</span></p>
                        <p><strong>网关:</strong> <span id="net-eth-gw">-</span></p>
                        <p><strong>MAC:</strong> <span id="net-eth-mac">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>📶 WiFi</h3>
                    <div class="card-content" id="wifi-info">
                        <p><strong>状态:</strong> <span id="net-wifi-status">-</span></p>
                        <p><strong>SSID:</strong> <span id="net-wifi-ssid">-</span></p>
                        <p><strong>IP:</strong> <span id="net-wifi-ip">-</span></p>
                        <p><strong>信号:</strong> <span id="net-wifi-rssi">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn" onclick="showWifiScan()">扫描网络</button>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🔀 DHCP 服务器</h3>
                    <div class="card-content" id="dhcp-info">
                        <p><strong>状态:</strong> <span id="net-dhcp-status">-</span></p>
                        <p><strong>客户端:</strong> <span id="net-dhcp-clients">-</span></p>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🌍 NAT</h3>
                    <div class="card-content" id="nat-info">
                        <p><strong>状态:</strong> <span id="net-nat-status">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn" id="nat-toggle-btn" onclick="toggleNat()">启用</button>
                    </div>
                </div>
            </div>
            
            <div class="section hidden" id="wifi-scan-section">
                <h2>WiFi 网络列表</h2>
                <table class="data-table">
                    <thead>
                        <tr><th>SSID</th><th>信号</th><th>加密</th><th>操作</th></tr>
                    </thead>
                    <tbody id="wifi-scan-results"></tbody>
                </table>
            </div>
        </div>
    `;
    
    await refreshNetworkPage();
}

async function refreshNetworkPage() {
    // 网络状态
    try {
        const status = await api.networkStatus();
        if (status.data) {
            const eth = status.data.ethernet || {};
            const wifi = status.data.wifi || {};
            
            document.getElementById('net-eth-status').textContent = eth.status || '-';
            document.getElementById('net-eth-ip').textContent = eth.ip || '-';
            document.getElementById('net-eth-gw').textContent = eth.gateway || '-';
            document.getElementById('net-eth-mac').textContent = eth.mac || '-';
            
            document.getElementById('net-wifi-status').textContent = wifi.connected ? '已连接' : '未连接';
            document.getElementById('net-wifi-ssid').textContent = wifi.ssid || '-';
            document.getElementById('net-wifi-ip').textContent = wifi.ip || '-';
            document.getElementById('net-wifi-rssi').textContent = wifi.rssi ? `${wifi.rssi} dBm` : '-';
        }
    } catch (e) { console.log('Network status error:', e); }
    
    // DHCP 状态
    try {
        const dhcp = await api.dhcpStatus();
        if (dhcp.data) {
            document.getElementById('net-dhcp-status').textContent = dhcp.data.enabled ? '运行中' : '已停止';
        }
        const clients = await api.dhcpClients();
        if (clients.data) {
            document.getElementById('net-dhcp-clients').textContent = 
                (clients.data.clients?.length || 0) + ' 个';
        }
    } catch (e) { console.log('DHCP error:', e); }
    
    // NAT 状态
    try {
        const nat = await api.natStatus();
        if (nat.data) {
            const enabled = nat.data.enabled;
            document.getElementById('net-nat-status').textContent = enabled ? '已启用' : '已禁用';
            document.getElementById('nat-toggle-btn').textContent = enabled ? '禁用' : '启用';
        }
    } catch (e) { console.log('NAT error:', e); }
}

async function showWifiScan() {
    const section = document.getElementById('wifi-scan-section');
    const tbody = document.getElementById('wifi-scan-results');
    
    section.classList.remove('hidden');
    tbody.innerHTML = '<tr><td colspan="4">扫描中...</td></tr>';
    
    try {
        const result = await api.wifiScan();
        if (result.data && result.data.networks) {
            tbody.innerHTML = result.data.networks.map(net => `
                <tr>
                    <td>${net.ssid}</td>
                    <td>${net.rssi} dBm</td>
                    <td>${net.auth || 'OPEN'}</td>
                    <td><button class="btn btn-small" onclick="connectWifi('${net.ssid}')">连接</button></td>
                </tr>
            `).join('');
        }
    } catch (e) {
        tbody.innerHTML = '<tr><td colspan="4">扫描失败</td></tr>';
    }
}

function connectWifi(ssid) {
    const password = prompt(`输入 ${ssid} 的密码:`);
    if (password !== null) {
        api.wifiConnect(ssid, password)
            .then(() => showToast('正在连接...', 'info'))
            .catch(e => showToast('连接失败: ' + e.message, 'error'));
    }
}

async function toggleNat() {
    try {
        const status = await api.natStatus();
        if (status.data?.enabled) {
            await api.natDisable();
            showToast('NAT 已禁用', 'success');
        } else {
            await api.natEnable();
            showToast('NAT 已启用', 'success');
        }
        await refreshNetworkPage();
    } catch (e) { showToast('操作失败', 'error'); }
}

// =========================================================================
//                         设备页面
// =========================================================================

async function loadDevicePage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-device">
            <h1>设备控制</h1>
            
            <div class="cards">
                <div class="card">
                    <h3>🖥️ AGX</h3>
                    <div class="card-content">
                        <p><strong>电源:</strong> <span id="dev-agx-power">-</span></p>
                        <p><strong>CPU:</strong> <span id="dev-agx-cpu">-</span></p>
                        <p><strong>GPU:</strong> <span id="dev-agx-gpu">-</span></p>
                        <p><strong>温度:</strong> <span id="dev-agx-temp">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn btn-success" onclick="devicePower('agx', true)">开机</button>
                        <button class="btn btn-danger" onclick="devicePower('agx', false)">关机</button>
                        <button class="btn btn-warning" onclick="deviceReset('agx')">重启</button>
                    </div>
                </div>
                
                <div class="card">
                    <h3>🔋 LPMU</h3>
                    <div class="card-content">
                        <p><strong>电源:</strong> <span id="dev-lpmu-power">-</span></p>
                    </div>
                    <div class="button-group">
                        <button class="btn btn-success" onclick="devicePower('lpmu', true)">开机</button>
                        <button class="btn btn-danger" onclick="devicePower('lpmu', false)">关机</button>
                    </div>
                </div>
            </div>
            
            <div class="section">
                <h2>🌀 风扇控制</h2>
                <div class="fans-grid" id="fans-grid"></div>
            </div>
            
            <div class="section">
                <h2>⚡ 电源状态</h2>
                <div class="power-info" id="power-info"></div>
            </div>
        </div>
    `;
    
    await refreshDevicePage();
    refreshInterval = setInterval(refreshDevicePage, 2000);
}

async function refreshDevicePage() {
    // 设备状态
    try {
        const status = await api.deviceStatus();
        if (status.data?.devices) {
            const agx = status.data.devices.find(d => d.name === 'agx');
            const lpmu = status.data.devices.find(d => d.name === 'lpmu');
            
            document.getElementById('dev-agx-power').textContent = agx?.powered ? '运行中' : '关机';
            document.getElementById('dev-lpmu-power').textContent = lpmu?.powered ? '运行中' : '关机';
        }
    } catch (e) { console.log('Device status error:', e); }
    
    // AGX 监控数据
    try {
        const agxData = await api.agxData();
        if (agxData.data) {
            document.getElementById('dev-agx-cpu').textContent = 
                agxData.data.cpu_usage ? `${agxData.data.cpu_usage}%` : '-';
            document.getElementById('dev-agx-gpu').textContent = 
                agxData.data.gpu_usage ? `${agxData.data.gpu_usage}%` : '-';
            document.getElementById('dev-agx-temp').textContent = 
                agxData.data.temperature ? `${agxData.data.temperature}°C` : '-';
        }
    } catch (e) { /* AGX 可能未连接 */ }
    
    // 风扇
    try {
        const fans = await api.fanStatus();
        const container = document.getElementById('fans-grid');
        if (fans.data?.fans) {
            container.innerHTML = fans.data.fans.map(fan => `
                <div class="fan-card">
                    <h4>风扇 ${fan.id}</h4>
                    <p>模式: ${fan.mode}</p>
                    <p>转速: ${fan.speed}%</p>
                    <p>RPM: ${fan.rpm || '-'}</p>
                    <input type="range" min="0" max="100" value="${fan.speed}" 
                           onchange="setFanSpeed(${fan.id}, this.value)">
                </div>
            `).join('');
        }
    } catch (e) { console.log('Fan error:', e); }
    
    // 电源
    try {
        const power = await api.powerStatus();
        const container = document.getElementById('power-info');
        if (power.data) {
            container.innerHTML = `
                <div class="power-card">
                    <p><strong>电压:</strong> ${power.data.voltage || '-'} V</p>
                    <p><strong>电流:</strong> ${power.data.current || '-'} A</p>
                    <p><strong>功率:</strong> ${power.data.power || '-'} W</p>
                </div>
            `;
        }
    } catch (e) { console.log('Power error:', e); }
}

async function devicePower(name, on) {
    try {
        await api.devicePower(name, on);
        showToast(`${name} ${on ? '开机' : '关机'} 命令已发送`, 'success');
        await refreshDevicePage();
    } catch (e) { showToast('操作失败: ' + e.message, 'error'); }
}

async function deviceReset(name) {
    if (confirm(`确定要重启 ${name} 吗？`)) {
        try {
            await api.deviceReset(name);
            showToast(`${name} 重启命令已发送`, 'success');
        } catch (e) { showToast('操作失败: ' + e.message, 'error'); }
    }
}

async function setFanSpeed(id, speed) {
    try {
        await api.fanSet(id, parseInt(speed));
    } catch (e) { showToast('设置风扇失败', 'error'); }
}

// =========================================================================
//                         配置页面
// =========================================================================

async function loadConfigPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-config">
            <h1>系统配置</h1>
            
            <div class="section">
                <h2>配置列表</h2>
                <div class="config-filter">
                    <input type="text" id="config-prefix" placeholder="输入前缀过滤 (如 network.)">
                    <button class="btn" onclick="filterConfig()">筛选</button>
                    <button class="btn" onclick="loadAllConfig()">显示全部</button>
                </div>
                <table class="data-table">
                    <thead>
                        <tr><th>键</th><th>值</th><th>类型</th><th>操作</th></tr>
                    </thead>
                    <tbody id="config-table-body"></tbody>
                </table>
            </div>
            
            <div class="section">
                <h2>添加/修改配置</h2>
                <form id="config-form" class="config-form" onsubmit="saveConfig(event)">
                    <div class="form-row">
                        <div class="form-group">
                            <label>键名</label>
                            <input type="text" id="cfg-key" required placeholder="network.hostname">
                        </div>
                        <div class="form-group">
                            <label>值</label>
                            <input type="text" id="cfg-value" required>
                        </div>
                        <div class="form-group">
                            <label>持久化</label>
                            <input type="checkbox" id="cfg-persist">
                        </div>
                    </div>
                    <button type="submit" class="btn btn-primary">保存</button>
                </form>
            </div>
        </div>
    `;
    
    await loadAllConfig();
}

async function loadAllConfig() {
    const tbody = document.getElementById('config-table-body');
    tbody.innerHTML = '<tr><td colspan="4">加载中...</td></tr>';
    
    try {
        const result = await api.configList();
        if (result.data?.items) {
            tbody.innerHTML = result.data.items.map(item => `
                <tr>
                    <td><code>${item.key}</code></td>
                    <td>${item.value}</td>
                    <td>${item.type || '-'}</td>
                    <td>
                        <button class="btn btn-small" onclick="editConfig('${item.key}', '${item.value}')">编辑</button>
                        <button class="btn btn-small btn-danger" onclick="deleteConfig('${item.key}')">删除</button>
                    </td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="4">暂无配置</td></tr>';
        }
    } catch (e) {
        tbody.innerHTML = '<tr><td colspan="4">加载失败</td></tr>';
    }
}

async function filterConfig() {
    const prefix = document.getElementById('config-prefix').value;
    const tbody = document.getElementById('config-table-body');
    
    try {
        const result = await api.configList(prefix);
        if (result.data?.items) {
            tbody.innerHTML = result.data.items.map(item => `
                <tr>
                    <td><code>${item.key}</code></td>
                    <td>${item.value}</td>
                    <td>${item.type || '-'}</td>
                    <td>
                        <button class="btn btn-small" onclick="editConfig('${item.key}', '${item.value}')">编辑</button>
                        <button class="btn btn-small btn-danger" onclick="deleteConfig('${item.key}')">删除</button>
                    </td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="4">暂无匹配配置</td></tr>';
        }
    } catch (e) {
        tbody.innerHTML = '<tr><td colspan="4">加载失败</td></tr>';
    }
}

function editConfig(key, value) {
    document.getElementById('cfg-key').value = key;
    document.getElementById('cfg-value').value = value;
}

async function saveConfig(e) {
    e.preventDefault();
    
    const key = document.getElementById('cfg-key').value;
    const value = document.getElementById('cfg-value').value;
    const persist = document.getElementById('cfg-persist').checked;
    
    try {
        await api.configSet(key, value, persist);
        showToast('配置已保存', 'success');
        await loadAllConfig();
        document.getElementById('config-form').reset();
    } catch (e) {
        showToast('保存失败: ' + e.message, 'error');
    }
}

async function deleteConfig(key) {
    if (confirm(`确定要删除配置 "${key}" 吗？`)) {
        try {
            await api.configDelete(key);
            showToast('配置已删除', 'success');
            await loadAllConfig();
        } catch (e) {
            showToast('删除失败: ' + e.message, 'error');
        }
    }
}

// =========================================================================
//                         安全页面
// =========================================================================

async function loadSecurityPage() {
    clearInterval(refreshInterval);
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="page-security">
            <h1>安全与连接</h1>
            
            <div class="section">
                <h2>🔑 SSH 连接测试</h2>
                <form id="ssh-test-form" class="ssh-form" onsubmit="testSsh(event)">
                    <div class="form-row">
                        <div class="form-group">
                            <label>主机</label>
                            <input type="text" id="ssh-host" required placeholder="192.168.1.100">
                        </div>
                        <div class="form-group">
                            <label>端口</label>
                            <input type="number" id="ssh-port" value="22">
                        </div>
                        <div class="form-group">
                            <label>用户名</label>
                            <input type="text" id="ssh-user" required placeholder="root">
                        </div>
                        <div class="form-group">
                            <label>密码</label>
                            <input type="password" id="ssh-password" required>
                        </div>
                    </div>
                    <button type="submit" class="btn btn-primary">测试连接</button>
                </form>
                <div id="ssh-result" class="result-box hidden"></div>
            </div>
            
            <div class="section">
                <h2>🔐 密钥管理</h2>
                <table class="data-table">
                    <thead>
                        <tr><th>ID</th><th>类型</th><th>备注</th><th>创建时间</th><th>操作</th></tr>
                    </thead>
                    <tbody id="keys-table-body"></tbody>
                </table>
            </div>
            
            <div class="section">
                <h2>📡 已知主机</h2>
                <table class="data-table">
                    <thead>
                        <tr><th>主机</th><th>端口</th><th>指纹</th><th>操作</th></tr>
                    </thead>
                    <tbody id="hosts-table-body"></tbody>
                </table>
            </div>
        </div>
    `;
    
    await refreshSecurityPage();
}

async function refreshSecurityPage() {
    // 密钥列表
    try {
        const keys = await api.keyList();
        const tbody = document.getElementById('keys-table-body');
        if (keys.data?.keys) {
            tbody.innerHTML = keys.data.keys.map(key => `
                <tr>
                    <td>${key.id}</td>
                    <td>${key.type}</td>
                    <td>${key.comment || '-'}</td>
                    <td>${key.created || '-'}</td>
                    <td><button class="btn btn-small btn-danger" onclick="deleteKey('${key.id}')">删除</button></td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="5">暂无密钥</td></tr>';
        }
    } catch (e) {
        document.getElementById('keys-table-body').innerHTML = '<tr><td colspan="5">加载失败</td></tr>';
    }
    
    // 已知主机
    try {
        const hosts = await api.hostsList();
        const tbody = document.getElementById('hosts-table-body');
        if (hosts.data?.hosts) {
            tbody.innerHTML = hosts.data.hosts.map(host => `
                <tr>
                    <td>${host.host}</td>
                    <td>${host.port}</td>
                    <td><code>${host.fingerprint?.substring(0, 20)}...</code></td>
                    <td><button class="btn btn-small btn-danger" onclick="removeHost('${host.id}')">移除</button></td>
                </tr>
            `).join('');
        } else {
            tbody.innerHTML = '<tr><td colspan="4">暂无已知主机</td></tr>';
        }
    } catch (e) {
        document.getElementById('hosts-table-body').innerHTML = '<tr><td colspan="4">加载失败</td></tr>';
    }
}

async function testSsh(e) {
    e.preventDefault();
    
    const host = document.getElementById('ssh-host').value;
    const port = parseInt(document.getElementById('ssh-port').value);
    const user = document.getElementById('ssh-user').value;
    const password = document.getElementById('ssh-password').value;
    
    const resultBox = document.getElementById('ssh-result');
    resultBox.classList.remove('hidden');
    resultBox.textContent = '测试中...';
    resultBox.className = 'result-box';
    
    try {
        const result = await api.sshTest(host, user, password, port);
        resultBox.textContent = '✅ 连接成功!';
        resultBox.classList.add('success');
    } catch (e) {
        resultBox.textContent = '❌ 连接失败: ' + e.message;
        resultBox.classList.add('error');
    }
}

async function deleteKey(id) {
    if (confirm('确定要删除此密钥吗？')) {
        try {
            await api.call('key.delete', { id }, 'POST');
            showToast('密钥已删除', 'success');
            await refreshSecurityPage();
        } catch (e) {
            showToast('删除失败', 'error');
        }
    }
}

async function removeHost(id) {
    if (confirm('确定要移除此主机记录吗？')) {
        try {
            await api.call('hosts.remove', { id }, 'POST');
            showToast('主机已移除', 'success');
            await refreshSecurityPage();
        } catch (e) {
            showToast('移除失败', 'error');
        }
    }
}

// =========================================================================
//                         工具函数
// =========================================================================

function formatUptime(ms) {
    if (!ms) return '-';
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    
    if (days > 0) return `${days}天 ${hours % 24}小时`;
    if (hours > 0) return `${hours}小时 ${minutes % 60}分钟`;
    if (minutes > 0) return `${minutes}分钟`;
    return `${seconds}秒`;
}

function formatBytes(bytes) {
    if (!bytes) return '-';
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
}

function showToast(message, type = 'info') {
    // 创建 toast 元素
    let toast = document.getElementById('toast');
    if (!toast) {
        toast = document.createElement('div');
        toast.id = 'toast';
        document.body.appendChild(toast);
    }
    
    toast.textContent = message;
    toast.className = `toast toast-${type} show`;
    
    setTimeout(() => {
        toast.classList.remove('show');
    }, 3000);
}

// =========================================================================
//                         终端页面
// =========================================================================

async function loadTerminalPage() {
    // 清理之前的终端实例
    if (webTerminal) {
        webTerminal.destroy();
        webTerminal = null;
    }
    
    const content = document.getElementById('page-content');
    content.innerHTML = `
        <div class="terminal-page">
            <div class="terminal-header">
                <h1>🖥️ Web 终端</h1>
                <div class="terminal-actions">
                    <button class="btn btn-sm" onclick="terminalClear()">清屏</button>
                    <button class="btn btn-sm btn-danger" onclick="terminalDisconnect()">断开</button>
                </div>
            </div>
            <div class="terminal-container" id="terminal-container"></div>
            <div class="terminal-help">
                <span>💡 提示: 输入 <code>help</code> 查看命令 | <code>Ctrl+C</code> 中断 | <code>Ctrl+L</code> 清屏 | <code>↑↓</code> 历史</span>
            </div>
        </div>
    `;
    
    // 初始化终端
    webTerminal = new WebTerminal('terminal-container');
    const ok = await webTerminal.init();
    if (ok) {
        webTerminal.connect();
    }
}

function terminalClear() {
    if (webTerminal && webTerminal.terminal) {
        webTerminal.terminal.clear();
        webTerminal.writePrompt();
    }
}

function terminalDisconnect() {
    if (webTerminal) {
        webTerminal.disconnect();
        showToast('终端已断开', 'info');
    }
}

// 暴露给 HTML onclick
window.closeLoginModal = closeLoginModal;
window.confirmReboot = confirmReboot;
window.serviceAction = serviceAction;
window.setBrightness = setBrightness;
window.clearLed = clearLed;
window.startEffect = startEffect;
window.applyColor = applyColor;
window.showWifiScan = showWifiScan;
window.connectWifi = connectWifi;
window.toggleNat = toggleNat;
window.devicePower = devicePower;
window.deviceReset = deviceReset;
window.setFanSpeed = setFanSpeed;
window.filterConfig = filterConfig;
window.loadAllConfig = loadAllConfig;
window.editConfig = editConfig;
window.saveConfig = saveConfig;
window.deleteConfig = deleteConfig;
window.testSsh = testSsh;
window.deleteKey = deleteKey;
window.removeHost = removeHost;
window.terminalClear = terminalClear;
window.terminalDisconnect = terminalDisconnect;
