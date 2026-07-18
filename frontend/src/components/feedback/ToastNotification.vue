<script setup lang="ts">
import { provide, reactive } from 'vue'

export interface Toast {
  id: string
  type: 'success' | 'error' | 'warning' | 'info'
  message: string
  duration?: number
}

interface ToastWithProgress extends Toast {
  remaining: number
  exiting: boolean
}

// Global toast state
const toasts = reactive<ToastWithProgress[]>([])
let toastCounter = 0

function addToast(toast: Omit<Toast, 'id'>) {
  const id = `toast-${++toastCounter}`
  const duration = toast.duration ?? 4000
  const item: ToastWithProgress = {
    ...toast,
    id,
    remaining: 100,
    exiting: false,
  }
  toasts.push(item)

  // Progress bar animation
  const startTime = Date.now()
  const interval = setInterval(() => {
    const elapsed = Date.now() - startTime
    const progress = Math.min((elapsed / duration) * 100, 100)
    item.remaining = 100 - progress
    if (progress >= 100) {
      clearInterval(interval)
      removeToast(id)
    }
  }, 30)

  // Store interval for cleanup
  ;(item as any)._interval = interval
}

function removeToast(id: string) {
  const idx = toasts.findIndex(t => t.id === id)
  if (idx === -1) return
  const item = toasts[idx]
  if ((item as any)._interval) clearInterval((item as any)._interval)
  item.exiting = true
  setTimeout(() => {
    const removeIdx = toasts.findIndex(t => t.id === id)
    if (removeIdx !== -1) toasts.splice(removeIdx, 1)
  }, 300)
}

// Provide global toast API
const toastApi = { addToast, removeToast }
provide('toast', toastApi)

const colorMap: Record<string, string> = {
  success: 'var(--color-success)',
  error: 'var(--color-error)',
  warning: 'var(--color-warning)',
  info: 'var(--color-info)',
}

function getColor(type: string) {
  return colorMap[type] || colorMap.info
}
</script>

<template>
  <Teleport to="body">
    <div class="toast-container" aria-live="polite">
      <TransitionGroup name="toast">
        <div
          v-for="toast in toasts"
          :key="toast.id"
          class="toast-item"
          :class="{ 'toast-exit': toast.exiting }"
        >
          <div class="toast-color-bar" :style="{ background: getColor(toast.type) }"></div>
          <div class="toast-icon">
            <span class="toast-icon-dot" :style="{ color: getColor(toast.type) }">●</span>
          </div>
          <div class="toast-content">
            <span class="toast-message">{{ toast.message }}</span>
          </div>
          <button class="toast-close" @click="removeToast(toast.id)">×</button>
          <div class="toast-progress">
            <div
              class="toast-progress-bar"
              :style="{
                width: toast.remaining + '%',
                background: getColor(toast.type),
              }"
            ></div>
          </div>
        </div>
      </TransitionGroup>
    </div>
  </Teleport>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.toast-container {
  position: fixed;
  top: var(--space-6);
  right: var(--space-6);
  z-index: var(--z-toast);
  display: flex;
  flex-direction: column;
  gap: var(--space-3);
  max-width: 400px;
  pointer-events: none;
}

.toast-item {
  position: relative;
  display: flex;
  align-items: center;
  gap: var(--space-3);
  padding: var(--space-4) var(--space-4) var(--space-4) var(--space-3);
  background: var(--glass-bg);
  backdrop-filter: blur(var(--glass-blur));
  -webkit-backdrop-filter: blur(var(--glass-blur));
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-md);
  box-shadow: var(--shadow-lg);
  pointer-events: auto;
  overflow: hidden;
  min-width: 300px;
}

.toast-color-bar {
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  width: 4px;
  border-radius: 4px 0 0 4px;
}

.toast-icon {
  flex-shrink: 0;
  font-size: 18px;
}

.toast-icon-dot {
  font-size: 10px;
}

.toast-content {
  flex: 1;
  min-width: 0;
}

.toast-message {
  color: var(--text-primary);
  font-size: 14px;
  line-height: 1.5;
  word-break: break-word;
}

.toast-close {
  flex-shrink: 0;
  width: 24px;
  height: 24px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: none;
  border: none;
  color: var(--text-tertiary);
  cursor: pointer;
  font-size: 18px;
  border-radius: var(--radius-sm);
  transition: all var(--duration-fast) var(--ease-default);
}

.toast-close:hover {
  background: var(--glass-bg-hover);
  color: var(--text-primary);
}

.toast-progress {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  height: 2px;
  background: rgba(255, 255, 255, 0.05);
}

.toast-progress-bar {
  height: 100%;
  transition: width 30ms linear;
  opacity: 0.6;
}

/* Transition animations */
.toast-enter-active {
  animation: toast-in 0.3s var(--ease-out);
}

.toast-leave-active {
  animation: toast-out 0.3s var(--ease-in);
}

.toast-exit {
  animation: toast-out 0.3s var(--ease-in) forwards;
}

@keyframes toast-in {
  from {
    opacity: 0;
    transform: translateX(100%);
  }
  to {
    opacity: 1;
    transform: translateX(0);
  }
}

@keyframes toast-out {
  from {
    opacity: 1;
    transform: translateX(0);
  }
  to {
    opacity: 0;
    transform: translateX(100%);
  }
}
</style>
