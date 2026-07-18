<template>
  <span class="streaming-text">
    <span class="stable-text">{{ stableText }}</span>
    <span v-if="freshText" class="fresh-text">{{ freshText }}</span>
    <span v-if="text" class="streaming-cursor"></span>
  </span>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  text: string
}>()

const FRESH_CHAR_COUNT = 3

const stableText = computed(() => {
  if (!props.text) return ''
  const len = props.text.length
  if (len <= FRESH_CHAR_COUNT) return ''
  return props.text.slice(0, len - FRESH_CHAR_COUNT)
})

const freshText = computed(() => {
  if (!props.text) return ''
  const len = props.text.length
  if (len <= FRESH_CHAR_COUNT) return props.text
  return props.text.slice(len - FRESH_CHAR_COUNT)
})
</script>

<style scoped>
@import "../styles/design-tokens.css";

.streaming-text {
  position: relative;
  color: var(--text-primary);
  line-height: 1.7;
}

.stable-text {
  /* already-rendered text, no special styling needed */
}

.fresh-text {
  filter: brightness(1.3);
  opacity: 0.95;
  transition: filter 0.4s ease-out, opacity 0.4s ease-out;
}

.streaming-cursor {
  display: inline-block;
  width: 2px;
  height: 1.2em;
  background: var(--brand-primary);
  animation: cursorBreathe 1.2s ease-in-out infinite;
  vertical-align: text-bottom;
  margin-left: 2px;
  border-radius: 1px;
  box-shadow: 0 0 8px rgba(99, 102, 241, 0.6);
}

@keyframes cursorBreathe {
  0%, 100% {
    opacity: 1;
    box-shadow: 0 0 8px rgba(99, 102, 241, 0.6);
  }
  50% {
    opacity: 0.3;
    box-shadow: 0 0 4px rgba(99, 102, 241, 0.3);
  }
}
</style>
