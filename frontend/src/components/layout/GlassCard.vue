<script setup lang="ts">
interface Props {
  variant?: 'default' | 'highlight' | 'warning' | 'success' | 'error'
  hoverable?: boolean
  glow?: boolean
  padding?: 'none' | 'sm' | 'md' | 'lg'
}

const props = withDefaults(defineProps<Props>(), {
  variant: 'default',
  hoverable: false,
  glow: false,
  padding: 'md',
})

const paddingMap: Record<string, string> = {
  none: '0',
  sm: '12px',
  md: '20px',
  lg: '28px',
}
</script>

<template>
  <div
    class="glass-card"
    :class="[
      `variant-${props.variant}`,
      { hoverable: props.hoverable },
      { glow: props.glow },
    ]"
    :style="{ padding: paddingMap[props.padding] }"
  >
    <slot />
  </div>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.glass-card {
  background: var(--glass-bg);
  backdrop-filter: blur(var(--glass-blur));
  -webkit-backdrop-filter: blur(var(--glass-blur));
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-lg);
  transition: all var(--duration-normal) var(--ease-default);
}

/* Variants */
.variant-highlight {
  border-color: var(--border-brand);
  background: rgba(99, 102, 241, 0.05);
}

.variant-warning {
  border-color: rgba(245, 158, 11, 0.4);
}

.variant-success {
  border-color: rgba(16, 185, 129, 0.4);
}

.variant-error {
  border-color: rgba(239, 68, 68, 0.4);
}

/* Hoverable */
.hoverable:hover {
  transform: translateY(-2px);
  box-shadow: var(--shadow-md);
  border-color: var(--border-strong);
}

.hoverable.glow:hover {
  box-shadow: var(--shadow-md), var(--shadow-glow-brand);
}

.variant-warning.hoverable.glow:hover {
  box-shadow: var(--shadow-md), var(--shadow-glow-warning);
}

.variant-success.hoverable.glow:hover {
  box-shadow: var(--shadow-md), var(--shadow-glow-success);
}

.variant-error.hoverable.glow:hover {
  box-shadow: var(--shadow-md), var(--shadow-glow-error);
}
</style>
