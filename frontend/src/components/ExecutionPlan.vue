<template>
  <div class="execution-plan" v-if="plan">
    <div class="plan-header">
      <span class="plan-icon">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><line x1="3" y1="9" x2="21" y2="9"/><line x1="9" y1="21" x2="9" y2="9"/>
        </svg>
      </span>
      <span class="plan-title">执行计划</span>
      <span class="plan-badge">{{ completedCount }}/{{ plan.tasks.length }}</span>
    </div>

    <!-- DAG Visual -->
    <div v-if="showDag" class="dag-container">
      <div ref="dagRef" class="mermaid-container"></div>
    </div>

    <button
      v-if="plan.tasks.length > 1"
      class="dag-toggle"
      @click="toggleDag"
    >
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <polygon points="12 2 22 8.5 22 15.5 12 22 2 15.5 2 8.5 12 2"/><line x1="12" y1="22" x2="12" y2="15.5"/><polyline points="22 8.5 12 15.5 2 8.5"/>
      </svg>
      {{ showDag ? '隐藏流程图' : '显示流程图' }}
    </button>

    <!-- Task Cards -->
    <div class="plan-tasks">
      <div
        v-for="task in plan.tasks"
        :key="task.id"
        class="task-card"
        :class="task.status"
      >
        <div class="task-header">
          <span class="task-status-icon">
            <template v-if="task.status === 'pending'">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/></svg>
            </template>
            <template v-else-if="task.status === 'running'">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="spin"><path d="M21 12a9 9 0 1 1-6.219-8.56"/></svg>
            </template>
            <template v-else-if="task.status === 'completed'">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="20 6 9 17 4 12"/></svg>
            </template>
            <template v-else-if="task.status === 'failed'">
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
            </template>
          </span>
          <span class="task-id">{{ task.id }}</span>
          <span class="task-skill">{{ task.skill }}</span>
          <span v-if="task.assigned_agent_id" class="task-agent">{{ task.assigned_agent_id }}</span>
        </div>

        <div class="task-description">{{ task.description }}</div>

        <div v-if="task.depends_on.length > 0" class="task-deps">
          <span v-for="dep in task.depends_on" :key="dep" class="dep-tag">
            ← {{ dep }}
          </span>
        </div>

        <div v-if="task.result && task.status !== 'pending'" class="task-result">
          {{ truncate(task.result, 200) }}
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, nextTick } from 'vue'
import type { ExecutionPlan } from '../types/proto'

const props = defineProps<{ plan: ExecutionPlan }>()

const showDag = ref(false)
const dagRef = ref<HTMLDivElement>()

const completedCount = computed(() =>
  props.plan.tasks.filter(t => t.status === 'completed' || t.status === 'failed').length
)

function generateMermaid(plan: ExecutionPlan): string {
  let mermaid = 'graph TD\n'
  mermaid += '  Q["🔍 Query"] --> N1\n'

  plan.tasks.forEach((task, i) => {
    const nodeId = `N${i + 1}`
    const icon = task.status === 'completed' ? '✅' :
                 task.status === 'running' ? '⏳' :
                 task.status === 'failed' ? '❌' : '○'
    mermaid += `  ${nodeId}["${icon} ${task.skill}\\n${task.description.slice(0, 30)}"]\n`

    for (const dep of task.depends_on) {
      const depIdx = plan.tasks.findIndex(t => t.id === dep)
      if (depIdx >= 0) {
        mermaid += `  N${depIdx + 1} --> ${nodeId}\n`
      }
    }
  })

  // Style
  mermaid += '  classDef done fill:#dcfce7,stroke:#22c55e,color:#166534\n'
  mermaid += '  classDef running fill:#dbeafe,stroke:#3b82f6,color:#1e40af\n'
  mermaid += '  classDef failed fill:#fee2e2,stroke:#ef4444,color:#991b1b\n'

  plan.tasks.forEach((task, i) => {
    if (task.status === 'completed') mermaid += `  class N${i + 1} done\n`
    if (task.status === 'running') mermaid += `  class N${i + 1} running\n`
    if (task.status === 'failed') mermaid += `  class N${i + 1} failed\n`
  })

  return mermaid
}

async function toggleDag() {
  showDag.value = !showDag.value
  if (showDag.value) {
    await nextTick()
    await renderMermaid()
  }
}

async function renderMermaid() {
  if (!dagRef.value) return
  try {
    const mermaid = await import('mermaid')
    mermaid.default.initialize({
      startOnLoad: false,
      theme: 'neutral',
      securityLevel: 'loose',
      flowchart: {
        useMaxWidth: true,
        htmlLabels: true,
        curve: 'basis',
      },
    })
    const { svg } = await mermaid.default.render(
      `dag-${Date.now()}`,
      generateMermaid(props.plan)
    )
    dagRef.value.innerHTML = svg
  } catch {
    dagRef.value.innerHTML = '<p style="color:#9ca3af;font-size:12px;text-align:center">流程图渲染失败</p>'
  }
}

function truncate(text: string, max: number): string {
  if (!text) return ''
  return text.length > max ? text.slice(0, max) + '...' : text
}
</script>

<style scoped>
.execution-plan {
  margin: 8px 0 12px;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  overflow: hidden;
  background: #fafafa;
}

.plan-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 14px;
  background: #f3f4f6;
  border-bottom: 1px solid #e5e7eb;
  font-size: 13px;
  font-weight: 600;
  color: #374151;
}

.plan-icon {
  display: flex;
  align-items: center;
  color: #6366f1;
}

.plan-badge {
  margin-left: auto;
  padding: 2px 8px;
  border-radius: 10px;
  background: #e5e7eb;
  font-size: 12px;
  font-weight: 500;
  color: #6b7280;
}

/* DAG */
.dag-toggle {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 100%;
  padding: 8px 14px;
  border: none;
  border-bottom: 1px solid #f3f4f6;
  background: #fafafa;
  color: #6366f1;
  font-size: 12px;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.15s;
}

.dag-toggle:hover {
  background: #f0f0ff;
}

.dag-container {
  padding: 12px;
  background: #fff;
  border-bottom: 1px solid #e5e7eb;
}

.mermaid-container {
  display: flex;
  justify-content: center;
  overflow-x: auto;
}

.mermaid-container :deep(svg) {
  max-width: 100%;
  height: auto;
}

/* Tasks */
.plan-tasks {
  display: flex;
  flex-direction: column;
}

.task-card {
  padding: 10px 14px;
  border-bottom: 1px solid #f3f4f6;
  transition: background 0.2s;
}

.task-card:last-child {
  border-bottom: none;
}

.task-card.running {
  background: #eff6ff;
}

.task-card.completed {
  background: #f0fdf4;
}

.task-card.failed {
  background: #fef2f2;
}

.task-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 4px;
  flex-wrap: wrap;
}

.task-status-icon {
  width: 18px;
  height: 18px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.task-card.pending .task-status-icon { color: #9ca3af; }
.task-card.running .task-status-icon { color: #3b82f6; }
.task-card.completed .task-status-icon { color: #22c55e; }
.task-card.failed .task-status-icon { color: #ef4444; }

.spin {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
}

.task-id {
  font-size: 12px;
  font-weight: 600;
  color: #6b7280;
  font-family: 'SF Mono', 'Fira Code', monospace;
}

.task-skill {
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 4px;
  background: #e5e7eb;
  color: #4b5563;
}

.task-agent {
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 4px;
  background: #dbeafe;
  color: #1e40af;
  font-family: monospace;
}

.task-description {
  font-size: 13px;
  color: #374151;
  line-height: 1.4;
}

.task-deps {
  display: flex;
  gap: 4px;
  margin-top: 4px;
  flex-wrap: wrap;
}

.dep-tag {
  font-size: 11px;
  color: #9ca3af;
}

.task-result {
  margin-top: 6px;
  font-size: 12px;
  color: #6b7280;
  line-height: 1.4;
  padding: 6px 8px;
  background: rgba(0, 0, 0, 0.03);
  border-radius: 6px;
  white-space: pre-wrap;
  word-break: break-word;
}
</style>
