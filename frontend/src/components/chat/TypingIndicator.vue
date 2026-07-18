<script setup lang="ts">
defineProps<{ text?: string }>()
</script>

<template>
  <div class="typing-indicator glass">
    <div class="typing-dots">
      <span></span><span></span><span></span>
    </div>
    <span class="typing-text gradient-text">{{ text || 'Agent 正在思考' }}</span>
  </div>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

@keyframes dotBounce {
  0%, 80%, 100% {
    transform: scale(0);
    opacity: 0.4;
  }
  40% {
    transform: scale(1);
    opacity: 1;
  }
}

.typing-indicator {
  display: inline-flex;
  align-items: center;
  gap: var(--space-3);
  padding: var(--space-2) var(--space-4);
  border-radius: var(--radius-lg);
}

.typing-dots {
  display: flex;
  align-items: center;
  gap: 4px;
}

.typing-dots span {
  width: 8px;
  height: 8px;
  border-radius: var(--radius-full);
  background: var(--brand-primary);
  animation: dotBounce 1.4s ease-in-out infinite;
}

.typing-dots span:nth-child(1) {
  animation-delay: 0s;
}

.typing-dots span:nth-child(2) {
  animation-delay: 0.2s;
}

.typing-dots span:nth-child(3) {
  animation-delay: 0.4s;
}

.typing-text {
  font-size: 13px;
  font-weight: 500;
}
</style>
