<script setup lang="ts">
import { provide, reactive, onUnmounted } from 'vue'

// Global progress bar state
const state = reactive({
  visible: false,
  progress: 0,
  completing: false,
})

let animFrame: number | null = null
let completeTimer: ReturnType<typeof setTimeout> | null = null

function start() {
  state.visible = true
  state.progress = 0
  state.completing = false
  // Simulate progress
  animateProgress()
}

function animateProgress() {
  if (animFrame) cancelAnimationFrame(animFrame)

  const step = () => {
    if (state.progress < 85) {
      // Slow down as we approach 85%
      const increment = Math.max(0.5, (85 - state.progress) * 0.02)
      state.progress = Math.min(state.progress + increment, 85)
      animFrame = requestAnimationFrame(step)
    }
  }
  animFrame = requestAnimationFrame(step)
}

function setProgress(value: number) {
  state.progress = Math.min(Math.max(value, 0), 100)
}

function finish() {
  if (animFrame) cancelAnimationFrame(animFrame)
  state.progress = 100
  state.completing = true

  completeTimer = setTimeout(() => {
    state.visible = false
    state.progress = 0
    state.completing = false
  }, 400)
}

onUnmounted(() => {
  if (animFrame) cancelAnimationFrame(animFrame)
  if (completeTimer) clearTimeout(completeTimer)
})

// Provide global API
provide('progressBar', { start, finish, setProgress })
</script>

<template>
  <Teleport to="body">
    <Transition name="progress-fade">
      <div v-if="state.visible" class="progress-bar-wrapper">
        <div
          class="progress-bar"
          :class="{ 'progress-completing': state.completing }"
          :style="{ width: state.progress + '%' }"
        ></div>
        <div
          v-if="state.completing"
          class="progress-bar-glow"
        ></div>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.progress-bar-wrapper {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  z-index: var(--z-toast);
  background: transparent;
  overflow: hidden;
}

.progress-bar {
  height: 100%;
  background: var(--brand-gradient);
  border-radius: 0 2px 2px 0;
  transition: width 0.3s var(--ease-out);
  box-shadow: 0 0 8px rgba(99, 102, 241, 0.5), 0 0 2px rgba(139, 92, 246, 0.3);
}

.progress-completing {
  transition: width 0.2s var(--ease-out);
  box-shadow: 0 0 16px rgba(99, 102, 241, 0.7), 0 0 4px rgba(139, 92, 246, 0.5);
}

.progress-bar-glow {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 100%;
  background: var(--brand-gradient);
  opacity: 0.6;
  animation: glow-pulse 0.4s ease-out;
}

@keyframes glow-pulse {
  0% {
    opacity: 1;
    box-shadow: 0 0 20px rgba(99, 102, 241, 0.8);
  }
  100% {
    opacity: 0;
    box-shadow: none;
  }
}

/* Fade transition */
.progress-fade-enter-active {
  transition: opacity 0.2s ease-out;
}

.progress-fade-leave-active {
  transition: opacity 0.3s ease-in;
}

.progress-fade-enter-from,
.progress-fade-leave-to {
  opacity: 0;
}
</style>
