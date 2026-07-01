<template>
  <div class="execution-plan" v-if="plan">
    <div class="plan-header">
      <span class="plan-icon">&#9881;</span>
      <span class="plan-title">执行计划</span>
      <span class="plan-badge">{{ completedCount }}/{{ plan.tasks.length }}</span>
    </div>

    <div class="plan-tasks">
      <div
        v-for="task in plan.tasks"
        :key="task.id"
        class="task-card"
        :class="task.status"
      >
        <div class="task-header">
          <span class="task-status-icon">
            <template v-if="task.status === 'pending'">&#9675;</template>
            <template v-else-if="task.status === 'running'">&#9696;</template>
            <template v-else-if="task.status === 'completed'">&#10003;</template>
            <template v-else-if="task.status === 'failed'">&#10007;</template>
          </span>
          <span class="task-id">{{ task.id }}</span>
          <span class="task-skill">{{ task.skill }}</span>
        </div>

        <div class="task-description">{{ task.description }}</div>

        <div v-if="task.depends_on.length > 0" class="task-deps">
          <span v-for="dep in task.depends_on" :key="dep" class="dep-tag">
            &larr; {{ dep }}
          </span>
        </div>

        <div v-if="task.result && task.status !== 'pending'" class="task-result">
          {{ truncate(task.result, 120) }}
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ExecutionPlan } from '../types/proto'

const props = defineProps<{
  plan: ExecutionPlan
}>()

const completedCount = computed(() =>
  props.plan.tasks.filter(t => t.status === 'completed' || t.status === 'failed').length
)

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
  font-size: 15px;
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

.plan-tasks {
  display: flex;
  flex-direction: column;
  gap: 0;
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
}

.task-status-icon {
  width: 18px;
  height: 18px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  flex-shrink: 0;
}

.task-card.pending .task-status-icon { color: #9ca3af; }
.task-card.running .task-status-icon { color: #3b82f6; }
.task-card.completed .task-status-icon { color: #22c55e; }
.task-card.failed .task-status-icon { color: #ef4444; }

.task-id {
  font-size: 12px;
  font-weight: 600;
  color: #6b7280;
  font-family: monospace;
}

.task-skill {
  font-size: 11px;
  padding: 1px 6px;
  border-radius: 4px;
  background: #e5e7eb;
  color: #4b5563;
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
  padding: 4px 8px;
  background: rgba(0, 0, 0, 0.03);
  border-radius: 4px;
  white-space: pre-wrap;
  word-break: break-word;
}
</style>
