<script setup lang="ts">
import { inject, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import SideNav from './SideNav.vue'

const progressBar = inject<any>('progressBar')
const router = useRouter()

const removeBeforeGuard = router.beforeEach(() => progressBar?.start())
const removeAfterGuard = router.afterEach(() => progressBar?.finish())

onUnmounted(() => {
  removeBeforeGuard()
  removeAfterGuard()
})
</script>

<template>
  <div class="app-layout">
    <SideNav />
    <main class="app-main">
      <slot />
    </main>
  </div>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.app-layout {
  display: flex;
  min-height: 100vh;
  background: var(--bg-primary);
}

.app-main {
  flex: 1;
  overflow-y: auto;
  position: relative;
}

/* Decorative background orbs */
.app-main::before {
  content: '';
  position: fixed;
  width: 600px;
  height: 600px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(99, 102, 241, 0.08), transparent 70%);
  top: -200px;
  right: -100px;
  animation: float 20s ease-in-out infinite;
  pointer-events: none;
  z-index: 0;
}

.app-main::after {
  content: '';
  position: fixed;
  width: 500px;
  height: 500px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(139, 92, 246, 0.06), transparent 70%);
  bottom: -150px;
  left: 20%;
  animation: float 25s ease-in-out infinite reverse;
  pointer-events: none;
  z-index: 0;
}

@keyframes float {
  0%,
  100% {
    transform: translateY(0) translateX(0);
  }
  33% {
    transform: translateY(-20px) translateX(10px);
  }
  66% {
    transform: translateY(10px) translateX(-15px);
  }
}

/* Tablet */
@media (max-width: 1024px) {
  .app-main {
    /* padding controlled by child pages */
  }
}

/* Mobile: bottom tab layout */
@media (max-width: 768px) {
  .app-layout {
    flex-direction: column-reverse;
  }

  .app-main {
    padding-bottom: calc(56px + var(--space-4));
    min-height: auto;
  }
}

/* Reduced motion */
@media (prefers-reduced-motion: reduce) {
  .app-main::before,
  .app-main::after {
    animation: none;
  }
}
</style>
