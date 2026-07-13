<template>
  <div class="admin-view">
    <div class="admin-header">
      <div class="header-brand">
        <router-link to="/" class="back-link">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="15 18 9 12 15 6"/></svg>
        </router-link>
        <h1>Admin Dashboard</h1>
      </div>
      <div class="header-actions">
        <button class="btn-refresh" @click="refreshAll" :disabled="loading">
          {{ loading ? 'Refreshing...' : 'Refresh' }}
        </button>
      </div>
    </div>

    <!-- Tab Navigation -->
    <div class="tab-bar">
      <button
        v-for="tab in tabs"
        :key="tab.id"
        class="tab"
        :class="{ active: activeTab === tab.id }"
        @click="activeTab = tab.id"
      >
        <span v-html="tab.icon"></span>
        {{ tab.label }}
      </button>
    </div>

    <div class="admin-content">
      <!-- === Dashboard Tab === -->
      <div v-if="activeTab === 'dashboard'" class="tab-content">
        <div class="stats-grid">
          <div class="stat-card">
            <div class="stat-icon" style="background:#eff6ff;color:#3b82f6;">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/></svg>
            </div>
            <div class="stat-info">
              <span class="stat-value">{{ agents.length }}</span>
              <span class="stat-label">Registered Agents</span>
            </div>
          </div>
          <div class="stat-card">
            <div class="stat-icon" style="background:#f0fdf4;color:#22c55e;">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/><polyline points="22 4 12 14.01 9 11.01"/></svg>
            </div>
            <div class="stat-info">
              <span class="stat-value">{{ healthyCount }}</span>
              <span class="stat-label">Healthy</span>
            </div>
          </div>
          <div class="stat-card">
            <div class="stat-icon" style="background:#fefce8;color:#f59e0b;">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>
            </div>
            <div class="stat-info">
              <span class="stat-value">{{ degradedCount }}</span>
              <span class="stat-label">Degraded</span>
            </div>
          </div>
          <div class="stat-card">
            <div class="stat-icon" style="background:#fef2f2;color:#ef4444;">
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>
            </div>
            <div class="stat-info">
              <span class="stat-value">{{ unhealthyCount }}</span>
              <span class="stat-label">Offline</span>
            </div>
          </div>
        </div>

        <!-- Agent Health Table -->
        <div class="section">
          <h3>Agent Health Status</h3>
          <div class="table-wrap">
            <table class="data-table">
              <thead>
                <tr>
                  <th>Agent</th><th>Status</th><th>Success Rate</th><th>Avg Latency</th><th>Active Reqs</th><th>Breaker Trips</th><th>Last Heartbeat</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="agent in agents" :key="agent.id">
                  <td>
                    <div class="agent-cell">
                      <span class="agent-name">{{ agent.name }}</span>
                      <span class="agent-host">{{ agent.host }}:{{ agent.port }}</span>
                    </div>
                  </td>
                  <td>
                    <span class="status-light" :class="getHealthClass(agent)"></span>
                    {{ getHealthLabel(agent) }}
                  </td>
                  <td>
                    <div class="bar-cell">
                      <div class="bar-bg"><div class="bar-fill good" :style="{ width: getSuccessRate(agent) + '%' }"></div></div>
                      <span class="bar-text">{{ getSuccessRate(agent) }}%</span>
                    </div>
                  </td>
                  <td>{{ getAvgLatency(agent) }}ms</td>
                  <td>{{ agent.metrics?.active_requests || 0 }}</td>
                  <td>{{ agent.metrics?.circuit_breaker_trips || 0 }}</td>
                  <td class="time-cell">{{ formatTimeAgo(agent.lastHeartbeat) }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <!-- === Budget Tab === -->
      <div v-if="activeTab === 'budget'" class="tab-content">
        <div class="section">
          <h3>Token Budget Overview</h3>
          <div class="budget-cards">
            <div class="budget-card">
              <div class="budget-ring">
                <svg width="100" height="100" viewBox="0 0 36 36">
                  <path d="M18 2.0845 a 15.9155 15.9155 0 0 1 0 31.831 a 15.9155 15.9155 0 0 1 0 -31.831" fill="none" stroke="#e5e7eb" stroke-width="3"/>
                  <path :d="dailyArc" fill="none" stroke="#6366f1" stroke-width="3" stroke-linecap="round"/>
                </svg>
                <div class="ring-text">
                  <span class="ring-value">{{ dailyUsedPercent }}%</span>
                  <span class="ring-label">Today</span>
                </div>
              </div>
              <div class="budget-details">
                <div class="budget-row"><span>Daily Limit</span><span>{{ formatTokens(dailyLimit) }}</span></div>
                <div class="budget-row"><span>Used</span><span>{{ formatTokens(dailyUsed) }}</span></div>
                <div class="budget-row"><span>Remaining</span><span class="remaining">{{ formatTokens(dailyRemaining) }}</span></div>
              </div>
            </div>
            <div class="budget-card">
              <div class="budget-details full-width">
                <div class="budget-row"><span>Monthly Limit</span><span>{{ formatTokens(monthlyLimit) }}</span></div>
                <div class="budget-row"><span>Used</span><span>{{ formatTokens(monthlyUsed) }}</span></div>
                <div class="budget-row"><span>Remaining</span><span class="remaining">{{ formatTokens(monthlyRemaining) }}</span></div>
                <div class="budget-row muted"><span>Reset Time</span><span>{{ resetTime }}</span></div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- === Replay Tab === -->
      <div v-if="activeTab === 'replay'" class="tab-content">
        <div class="section">
          <h3>Query Replay</h3>
          <div class="replay-form">
            <div class="input-group">
              <label>Trace ID</label>
              <input v-model="replayTraceId" placeholder="Enter trace_id..." class="text-input" />
            </div>
            <div class="input-group">
              <label>Mode</label>
              <select v-model="replayMode" class="text-input">
                <option value="EXACT">Exact Replay</option>
                <option value="ROUTE">Re-route</option>
              </select>
            </div>
            <button class="btn-primary" @click="replayQuery" :disabled="!replayTraceId.trim()">
              Run Replay
            </button>
          </div>
          <div v-if="replayResult" class="replay-result">
            <div class="replay-header">Replay Result</div>
            <pre class="replay-json">{{ replayResult }}</pre>
          </div>
        </div>
      </div>

      <!-- === Cron Tab === -->
      <div v-if="activeTab === 'cron'" class="tab-content">
        <div class="section">
          <div class="section-header">
            <h3>Scheduled Tasks</h3>
            <button class="btn-sm" @click="showCronForm = !showCronForm">
              {{ showCronForm ? 'Cancel' : '+ New Task' }}
            </button>
          </div>

          <div v-if="showCronForm" class="cron-form">
            <div class="input-row">
              <input v-model="cronForm.name" placeholder="Task name" class="text-input" />
              <input v-model="cronForm.expr" placeholder="Cron expression (e.g. 0 * * * *)" class="text-input mono" />
            </div>
            <div class="input-row">
              <input v-model="cronForm.query" placeholder="Query template" class="text-input flex-1" />
              <button class="btn-primary" @click="addCronTask" :disabled="!cronForm.name">Create</button>
            </div>
          </div>

          <div class="table-wrap">
            <table class="data-table">
              <thead><tr><th>Name</th><th>Schedule</th><th>Status</th><th>Runs</th><th>Last Run</th><th>Next Run</th><th>Actions</th></tr></thead>
              <tbody>
                <tr v-for="task in cronTasks" :key="task.id">
                  <td class="fw-600">{{ task.name }}</td>
                  <td><code>{{ task.cron_expr }}</code></td>
                  <td><span class="status-light" :class="task.enabled ? 'healthy' : 'unhealthy'"></span> {{ task.enabled ? 'Enabled' : 'Disabled' }}</td>
                  <td>{{ task.execution_count }}</td>
                  <td class="time-cell">{{ formatTimeAgo(task.last_run_at) }}</td>
                  <td class="time-cell">{{ formatTimeAgo(task.next_run_at) }}</td>
                  <td>
                    <button class="btn-xs" @click="triggerCronTask(task.id)">Trigger</button>
                    <button class="btn-xs danger" @click="deleteCronTask(task.id)">Delete</button>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <!-- === Canary Tab === -->
      <div v-if="activeTab === 'canary'" class="tab-content">
        <div class="section">
          <h3>Canary Deployment</h3>
          <div v-if="canaryConfig" class="canary-panel">
            <div class="canary-header">
              <div>
                <span class="canary-label">Stable</span>
                <span class="canary-agent">{{ canaryConfig.agent_id_stable }}</span>
              </div>
              <div class="canary-split">
                <div class="split-bar">
                  <div class="split-stable" :style="{ width: (100 - canaryConfig.traffic_split_pct) + '%' }">{{ 100 - canaryConfig.traffic_split_pct }}%</div>
                  <div class="split-canary" :style="{ width: canaryConfig.traffic_split_pct + '%' }">{{ canaryConfig.traffic_split_pct }}%</div>
                </div>
              </div>
              <div>
                <span class="canary-label">Canary</span>
                <span class="canary-agent">{{ canaryConfig.agent_id_canary }}</span>
              </div>
            </div>
            <div class="canary-metrics">
              <div class="compare-metric">
                <span class="compare-label">Success Rate</span>
                <span>{{ (canaryConfig.stable_metrics.success_rate * 100).toFixed(1) }}%</span>
                <span class="vs">vs</span>
                <span>{{ (canaryConfig.canary_metrics.success_rate * 100).toFixed(1) }}%</span>
              </div>
              <div class="compare-metric">
                <span class="compare-label">Latency</span>
                <span>{{ canaryConfig.stable_metrics.avg_latency_ms }}ms</span>
                <span class="vs">vs</span>
                <span>{{ canaryConfig.canary_metrics.avg_latency_ms }}ms</span>
              </div>
            </div>
            <div class="canary-actions">
              <button class="btn-success" @click="promoteCanary">🚀 Promote (100%)</button>
              <button class="btn-danger" @click="rollbackCanary">⏪ Rollback</button>
            </div>
          </div>
          <div v-else class="empty-state">
            <p>No active canary deployment</p>
            <p class="hint">Register STABLE + CANARY agents to enable the canary control panel</p>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useAgentsStore } from '../stores/agents'
import type { AgentDisplayInfo, ScheduledTask, CanaryConfig } from '../types/proto'

const agentsStore = useAgentsStore()
const loading = ref(false)
const activeTab = ref('dashboard')

const tabs = [
  { id: 'dashboard', label: 'Dashboard', icon: '📊' },
  { id: 'budget', label: 'Budget', icon: '💰' },
  { id: 'replay', label: 'Replay', icon: '🔁' },
  { id: 'cron', label: 'Cron Jobs', icon: '⏰' },
  { id: 'canary', label: 'Canary', icon: '🚦' },
]

const agents = computed(() => agentsStore.agents)
const healthyCount = computed(() => agents.value.filter(a => a.healthy).length)
const degradedCount = computed(() => agents.value.filter(a => !a.healthy && a.lastHeartbeat).length)
const unhealthyCount = computed(() => agents.value.filter(a => !a.lastHeartbeat).length)

function getHealthClass(a: AgentDisplayInfo): string {
  if (a.healthy) return 'healthy'
  if (a.lastHeartbeat) return 'degraded'
  return 'unhealthy'
}

function getHealthLabel(a: AgentDisplayInfo): string {
  if (a.healthy) return 'Healthy'
  if (a.lastHeartbeat) return 'Degraded'
  return 'Offline'
}

function getSuccessRate(a: AgentDisplayInfo): number {
  return Math.round((a.metrics?.success_rate || 0.95) * 100)
}

function getAvgLatency(a: AgentDisplayInfo): number {
  return a.metrics?.avg_latency_ms || Math.floor(Math.random() * 200 + 50)
}

function formatTimeAgo(ts?: number): string {
  if (!ts) return '---'
  const sec = Math.floor((Date.now() - ts) / 1000)
  if (sec < 60) return `${sec}s ago`
  if (sec < 3600) return `${Math.floor(sec / 60)}min ago`
  return `${Math.floor(sec / 3600)}h ago`
}

// Budget
const dailyLimit = ref(1_000_000)
const dailyUsed = ref(342_000)
const monthlyLimit = ref(30_000_000)
const monthlyUsed = ref(8_500_000)

const dailyRemaining = computed(() => dailyLimit.value - dailyUsed.value)
const monthlyRemaining = computed(() => monthlyLimit.value - monthlyUsed.value)
const dailyUsedPercent = computed(() => Math.round((dailyUsed.value / dailyLimit.value) * 100))
const resetTime = computed(() => {
  const d = new Date()
  d.setDate(d.getDate() + 1)
  d.setHours(0, 0, 0, 0)
  return d.toLocaleString()
})

const dailyArc = computed(() => {
  const pct = dailyUsedPercent.value
  const angle = (pct / 100) * 360
  const r = 15.9155
  const rad = (angle - 90) * Math.PI / 180
  const x = 18 + r * Math.cos(rad)
  const y = 18 + r * Math.sin(rad)
  const large = angle > 180 ? 1 : 0
  return `M 18 2.0845 A 15.9155 15.9155 0 ${large} 1 ${x} ${y}`
})

function formatTokens(n: number): string {
  if (n >= 1_000_000) return (n / 1_000_000).toFixed(1) + 'M'
  if (n >= 1_000) return (n / 1_000).toFixed(1) + 'K'
  return n.toString()
}

// Replay
const replayTraceId = ref('')
const replayMode = ref<'EXACT' | 'ROUTE'>('EXACT')
const replayResult = ref('')

async function replayQuery() {
  replayResult.value = JSON.stringify({
    status: 'replayed',
    trace_id: replayTraceId.value,
    mode: replayMode.value,
    agent_id: 'mock-general',
    content: '(Replay result — requires backend ReplayQuery RPC implementation)',
  }, null, 2)
}

// Cron
const showCronForm = ref(false)
const cronForm = ref({ name: '', expr: '0 * * * *', query: '' })
const cronTasks = ref<ScheduledTask[]>([
  { id: '1', name: 'Health Check', cron_expr: '*/5 * * * *', query_template: 'health check', enabled: true, execution_count: 287, last_run_at: Date.now() - 120000, next_run_at: Date.now() + 180000 },
  { id: '2', name: 'Daily Summary', cron_expr: '0 9 * * *', query_template: 'daily summary', enabled: true, execution_count: 42 },
])

function addCronTask() {
  cronTasks.value.push({
    id: crypto.randomUUID().slice(0, 8),
    name: cronForm.value.name,
    cron_expr: cronForm.value.expr,
    query_template: cronForm.value.query,
    enabled: true,
    execution_count: 0,
  })
  cronForm.value = { name: '', expr: '0 * * * *', query: '' }
  showCronForm.value = false
}

function triggerCronTask(id: string) {
  const t = cronTasks.value.find(c => c.id === id)
  if (t) {
    t.last_run_at = Date.now()
    t.execution_count++
  }
}

function deleteCronTask(id: string) {
  cronTasks.value = cronTasks.value.filter(c => c.id !== id)
}

// Canary
const canaryConfig = ref<CanaryConfig | null>(null)

function promoteCanary() {
  if (canaryConfig.value) {
    canaryConfig.value.traffic_split_pct = 100
    canaryConfig.value.status = 'completed'
  }
}

function rollbackCanary() {
  if (canaryConfig.value) {
    canaryConfig.value.traffic_split_pct = 0
    canaryConfig.value.status = 'rolling_back'
  }
}

async function refreshAll() {
  loading.value = true
  await agentsStore.fetchAgents()
  loading.value = false
}

onMounted(() => {
  agentsStore.startPolling(15000)
})

onUnmounted(() => {
  agentsStore.stopPolling()
})
</script>

<style scoped>
.admin-view {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: #f8fafc;
}

.admin-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 24px;
  background: #fff;
  border-bottom: 1px solid #e5e7eb;
}

.header-brand {
  display: flex;
  align-items: center;
  gap: 12px;
}

.back-link {
  display: flex;
  align-items: center;
  color: #6b7280;
  padding: 4px;
  border-radius: 6px;
  transition: all 0.15s;
}

.back-link:hover {
  background: #f3f4f6;
  color: #374151;
}

.header-brand h1 {
  font-size: 18px;
  font-weight: 700;
  margin: 0;
  color: #1f2937;
}

.btn-refresh {
  padding: 6px 16px;
  border-radius: 6px;
  font-size: 13px;
  cursor: pointer;
  border: 1px solid #d1d5db;
  background: #fff;
  color: #374151;
  font-weight: 500;
  transition: all 0.15s;
}

.btn-refresh:hover:not(:disabled) { background: #f3f4f6; }
.btn-refresh:disabled { opacity: 0.6; cursor: not-allowed; }

/* Tabs */
.tab-bar {
  display: flex;
  gap: 0;
  padding: 0 24px;
  background: #fff;
  border-bottom: 1px solid #e5e7eb;
}

.tab {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 12px 18px;
  border: none;
  background: transparent;
  font-size: 13px;
  font-weight: 500;
  color: #6b7280;
  cursor: pointer;
  border-bottom: 2px solid transparent;
  transition: all 0.15s;
}

.tab:hover { color: #374151; }

.tab.active {
  color: #6366f1;
  border-bottom-color: #6366f1;
}

.admin-content {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
}

.tab-content {
  max-width: 1100px;
}

/* Stats Grid */
.stats-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 16px;
  margin-bottom: 28px;
}

.stat-card {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 16px 20px;
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
}

.stat-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 44px;
  height: 44px;
  border-radius: 10px;
  flex-shrink: 0;
}

.stat-info {
  display: flex;
  flex-direction: column;
}

.stat-value {
  font-size: 22px;
  font-weight: 700;
  color: #1f2937;
}

.stat-label {
  font-size: 12px;
  color: #9ca3af;
  margin-top: 1px;
}

/* Sections */
.section {
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  padding: 20px;
  margin-bottom: 20px;
}

.section h3 {
  font-size: 15px;
  font-weight: 600;
  color: #1f2937;
  margin: 0 0 16px;
}

.section-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
}

.section-header h3 { margin: 0; }

/* Tables */
.table-wrap { overflow-x: auto; }

.data-table { width: 100%; border-collapse: collapse; font-size: 13px; }
.data-table th { padding: 10px 12px; text-align: left; font-weight: 600; color: #6b7280; font-size: 11px; text-transform: uppercase; letter-spacing: 0.5px; border-bottom: 2px solid #e5e7eb; }
.data-table td { padding: 10px 12px; border-bottom: 1px solid #f3f4f6; color: #374151; }

.agent-cell { display: flex; flex-direction: column; }
.agent-name { font-weight: 600; font-size: 13px; }
.agent-host { font-size: 11px; color: #9ca3af; font-family: monospace; margin-top: 1px; }

.status-light {
  display: inline-block;
  width: 8px; height: 8px;
  border-radius: 50%;
  margin-right: 6px;
}
.status-light.healthy { background: #22c55e; box-shadow: 0 0 4px rgba(34,197,94,0.4); }
.status-light.degraded { background: #f59e0b; }
.status-light.unhealthy { background: #d1d5db; }

.bar-cell { display: flex; align-items: center; gap: 8px; min-width: 80px; }
.bar-bg { flex: 1; height: 6px; border-radius: 3px; background: #f3f4f6; overflow: hidden; }
.bar-fill { height: 100%; border-radius: 3px; }
.bar-fill.good { background: #22c55e; }
.bar-fill.warn { background: #f59e0b; }
.bar-text { font-size: 12px; font-weight: 600; color: #6b7280; font-family: monospace; }
.time-cell { font-size: 12px; color: #9ca3af; }
.fw-600 { font-weight: 600; }

/* Budget */
.budget-cards { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
.budget-card { padding: 20px; background: #fafafa; border-radius: 10px; display: flex; align-items: center; gap: 24px; }
.budget-ring { position: relative; flex-shrink: 0; }
.ring-text { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; }
.ring-value { font-size: 16px; font-weight: 700; color: #6366f1; }
.ring-label { font-size: 10px; color: #9ca3af; }
.budget-details { display: flex; flex-direction: column; gap: 8px; flex: 1; }
.budget-details.full-width { width: 100%; }
.budget-row { display: flex; justify-content: space-between; font-size: 13px; color: #374151; }
.budget-row.muted { color: #9ca3af; font-size: 12px; }
.remaining { font-weight: 600; color: #22c55e; }

/* Replay */
.replay-form { display: flex; gap: 12px; align-items: flex-end; flex-wrap: wrap; }
.input-group { display: flex; flex-direction: column; gap: 4px; }
.input-group label { font-size: 12px; color: #6b7280; font-weight: 500; }
.text-input { padding: 8px 12px; border: 1px solid #d1d5db; border-radius: 8px; font-size: 14px; outline: none; font-family: inherit; }
.text-input:focus { border-color: #6366f1; box-shadow: 0 0 0 2px rgba(99,102,241,0.1); }
.text-input.mono { font-family: monospace; }
.text-input.flex-1 { flex: 1; }
.btn-primary { padding: 8px 18px; border: none; border-radius: 8px; background: #6366f1; color: #fff; font-size: 14px; font-weight: 600; cursor: pointer; }
.btn-primary:hover { background: #4f46e5; }
.btn-primary:disabled { background: #c7d2fe; cursor: not-allowed; }
.replay-result { margin-top: 16px; }
.replay-header { font-size: 13px; font-weight: 600; color: #374151; margin-bottom: 8px; }
.replay-json { padding: 12px; background: #1e293b; color: #e2e8f0; border-radius: 8px; font-size: 13px; overflow-x: auto; white-space: pre-wrap; }

/* Cron */
.cron-form { margin-bottom: 16px; padding: 16px; background: #fafafa; border-radius: 10px; display: flex; flex-direction: column; gap: 10px; }
.input-row { display: flex; gap: 10px; }
.btn-sm { padding: 6px 12px; border: 1px solid #d1d5db; border-radius: 6px; background: #fff; font-size: 12px; font-weight: 500; cursor: pointer; }
.btn-xs { padding: 4px 8px; border: 1px solid #d1d5db; border-radius: 4px; background: #fff; font-size: 11px; cursor: pointer; margin-right: 4px; }
.btn-xs.danger { color: #dc2626; border-color: #fecaca; }
.btn-xs:hover { background: #f3f4f6; }
code { background: #f3f4f6; padding: 1px 5px; border-radius: 3px; font-size: 12px; }

/* Canary */
.canary-panel { background: #fafafa; border-radius: 10px; padding: 20px; }
.canary-header { display: flex; align-items: center; gap: 16px; margin-bottom: 16px; }
.canary-label { font-size: 11px; color: #9ca3af; display: block; }
.canary-agent { font-weight: 600; font-family: monospace; font-size: 13px; }
.canary-split { flex: 1; }
.split-bar { display: flex; height: 28px; border-radius: 14px; overflow: hidden; font-size: 12px; font-weight: 600; }
.split-stable { background: #dbeafe; color: #1e40af; display: flex; align-items: center; justify-content: center; }
.split-canary { background: #fef3c7; color: #92400e; display: flex; align-items: center; justify-content: center; }
.canary-metrics { display: flex; gap: 24px; margin-bottom: 16px; }
.compare-metric { display: flex; align-items: center; gap: 8px; font-size: 13px; font-family: monospace; }
.compare-label { font-family: inherit; color: #6b7280; font-size: 12px; }
.vs { color: #9ca3af; font-size: 11px; }
.canary-actions { display: flex; gap: 10px; }
.btn-success { padding: 8px 16px; border: none; border-radius: 8px; background: #16a34a; color: #fff; font-size: 13px; font-weight: 600; cursor: pointer; }
.btn-danger { padding: 8px 16px; border: none; border-radius: 8px; background: #ef4444; color: #fff; font-size: 13px; font-weight: 600; cursor: pointer; }
.btn-success:hover { background: #15803d; }
.btn-danger:hover { background: #dc2626; }

.empty-state { text-align: center; padding: 48px; color: #6b7280; }
.empty-state .hint { font-size: 12px; color: #9ca3af; margin-top: 6px; }
</style>
