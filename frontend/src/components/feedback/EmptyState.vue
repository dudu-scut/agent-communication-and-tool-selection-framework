<script setup lang="ts">
import { Icon } from '@iconify/vue'

withDefaults(defineProps<{
  icon?: string
  title: string
  description?: string
  actionText?: string
}>(), {
  icon: 'mdi:inbox-outline',
})

defineEmits<{
  action: []
}>()
</script>

<template>
  <div class="empty-state">
    <div class="empty-icon-wrapper">
      <Icon :icon="icon" class="empty-icon" />
    </div>
    <h3 class="empty-title">{{ title }}</h3>
    <p v-if="description" class="empty-description">{{ description }}</p>
    <button
      v-if="actionText"
      class="empty-action"
      @click="$emit('action')"
    >
      {{ actionText }}
    </button>
  </div>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: var(--space-16) var(--space-8);
  text-align: center;
  gap: var(--space-4);
}

.empty-icon-wrapper {
  width: 88px;
  height: 88px;
  border-radius: var(--radius-full);
  background: linear-gradient(135deg, rgba(99, 102, 241, 0.12), rgba(168, 85, 247, 0.12));
  border: 1px solid rgba(99, 102, 241, 0.2);
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: var(--space-4);
  animation: float 3s ease-in-out infinite;
}

.empty-icon {
  font-size: 40px;
  background: var(--brand-gradient);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.empty-title {
  font-size: 20px;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
  line-height: 1.4;
}

.empty-description {
  font-size: 14px;
  color: var(--text-secondary);
  max-width: 360px;
  line-height: 1.6;
  margin: 0;
}

.empty-action {
  margin-top: var(--space-4);
  padding: var(--space-3) var(--space-6);
  font-size: 14px;
  font-weight: 500;
  color: var(--text-primary);
  background: transparent;
  border: 1px solid transparent;
  border-radius: var(--radius-md);
  cursor: pointer;
  position: relative;
  transition: all var(--duration-normal) var(--ease-default);
  background-clip: padding-box;
}

.empty-action::before {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: inherit;
  padding: 1px;
  background: var(--brand-gradient);
  -webkit-mask:
    linear-gradient(#fff 0 0) content-box,
    linear-gradient(#fff 0 0);
  -webkit-mask-composite: xor;
  mask-composite: exclude;
  pointer-events: none;
}

.empty-action:hover {
  background: rgba(99, 102, 241, 0.1);
  box-shadow: var(--shadow-glow-brand);
}

@keyframes float {
  0%, 100% {
    transform: translateY(0);
  }
  50% {
    transform: translateY(-6px);
  }
}
</style>
