<template>
  <div class="login-view">
    <!-- Background orbs -->
    <div class="login-bg-orb-1" />
    <div class="login-bg-orb-2" />
    <div class="login-bg-orb-3" />

    <div class="login-card">
      <div class="card-top-glow" />
      <h1 class="login-title">NexusAI</h1>
      <p class="login-tagline">多Agent智能协作平台</p>
      <p class="login-subtitle">{{ isRegister ? 'Create Account' : 'Sign in to continue' }}</p>

      <form class="login-form" @submit.prevent="handleSubmit">
        <div class="form-group">
          <label for="username">Username</label>
          <input
            id="username"
            v-model="form.username"
            type="text"
            placeholder="Enter username"
            autocomplete="username"
            required
          />
        </div>

        <div v-if="isRegister" class="form-group">
          <label for="displayName">Display Name <span class="optional">(optional)</span></label>
          <input
            id="displayName"
            v-model="form.displayName"
            type="text"
            placeholder="Enter display name"
            autocomplete="name"
          />
        </div>

        <div class="form-group">
          <label for="password">Password</label>
          <input
            id="password"
            v-model="form.password"
            type="password"
            placeholder="Enter password"
            autocomplete="current-password"
            required
          />
        </div>

        <p v-if="error" class="error-message">{{ error }}</p>
        <p v-if="success" class="success-message">{{ success }}</p>

        <button type="submit" class="btn-submit" :disabled="loading">
          {{ loading ? 'Please wait...' : (isRegister ? 'Register' : 'Sign In') }}
        </button>
      </form>

      <p class="toggle-mode">
        <template v-if="isRegister">
          Already have an account? <a href="#" @click.prevent="switchToLogin">Sign in</a>
        </template>
        <template v-else>
          Don't have an account? <a href="#" @click.prevent="switchToRegister">Register</a>
        </template>
      </p>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '../stores/auth'

const router = useRouter()
const auth = useAuthStore()

const isRegister = ref(false)
const loading = ref(false)
const error = ref('')
const success = ref('')

const form = reactive({
  username: '',
  password: '',
  displayName: '',
})

function switchToLogin() {
  isRegister.value = false
  error.value = ''
  success.value = ''
}

function switchToRegister() {
  isRegister.value = true
  error.value = ''
  success.value = ''
}

async function handleSubmit() {
  if (!form.username.trim() || !form.password.trim()) {
    error.value = 'Please enter username and password'
    return
  }

  if (form.password.length < 6) {
    error.value = 'Password must be at least 6 characters'
    return
  }

  if (isRegister.value && form.username.trim().length < 3) {
    error.value = 'Username must be at least 3 characters'
    return
  }

  loading.value = true
  error.value = ''
  success.value = ''

  try {
    if (isRegister.value) {
      const err = await auth.register(form.username.trim(), form.password, form.displayName.trim())
      if (err) {
        error.value = err
        return
      }
      // Registration successful — switch to login
      isRegister.value = false
      form.password = ''
      success.value = 'Registration successful, please sign in'
    } else {
      const err = await auth.login(form.username.trim(), form.password)
      if (err) {
        error.value = err
        return
      }
      router.push('/')
    }
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
@import "../styles/design-tokens.css";

/* ===== Background Orbs ===== */
.login-bg-orb-1 {
  position: fixed;
  width: 500px;
  height: 500px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(99, 102, 241, 0.15), transparent 70%);
  top: -150px;
  left: -100px;
  animation: floatOrb1 25s ease-in-out infinite;
  pointer-events: none;
  z-index: 0;
}

.login-bg-orb-2 {
  position: fixed;
  width: 400px;
  height: 400px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(168, 85, 247, 0.12), transparent 70%);
  bottom: -100px;
  right: -80px;
  animation: floatOrb2 30s ease-in-out infinite;
  pointer-events: none;
  z-index: 0;
}

.login-bg-orb-3 {
  position: fixed;
  width: 350px;
  height: 350px;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(59, 130, 246, 0.1), transparent 70%);
  top: 50%;
  left: 60%;
  animation: floatOrb3 20s ease-in-out infinite;
  pointer-events: none;
  z-index: 0;
}

@keyframes floatOrb1 {
  0%, 100% { transform: translate(0, 0) scale(1); }
  33% { transform: translate(60px, 40px) scale(1.05); }
  66% { transform: translate(-30px, 70px) scale(0.95); }
}

@keyframes floatOrb2 {
  0%, 100% { transform: translate(0, 0) scale(1); }
  33% { transform: translate(-50px, -30px) scale(1.08); }
  66% { transform: translate(40px, -60px) scale(0.92); }
}

@keyframes floatOrb3 {
  0%, 100% { transform: translate(0, 0) scale(1); }
  50% { transform: translate(-70px, 50px) scale(1.1); }
}

/* ===== Login View ===== */
.login-view {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  background: var(--bg-primary);
  padding: 1rem;
  position: relative;
  overflow: hidden;
}

/* ===== Login Card ===== */
.login-card {
  position: relative;
  background: rgba(255, 255, 255, 0.03);
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  border-radius: var(--radius-lg);
  box-shadow:
    0 8px 32px rgba(0, 0, 0, 0.3),
    0 0 0 1px rgba(255, 255, 255, 0.06),
    inset 0 1px 0 rgba(255, 255, 255, 0.05);
  padding: 2.5rem 2rem;
  width: 100%;
  max-width: 400px;
  z-index: 1;
  animation: slideUp 0.6s var(--ease-out) both;
  transition: transform 0.3s ease, box-shadow 0.3s ease;
}

.login-card:hover {
  transform: translateY(-4px);
  box-shadow:
    0 12px 40px rgba(0, 0, 0, 0.35),
    0 0 0 1px rgba(255, 255, 255, 0.08),
    0 0 30px rgba(99, 102, 241, 0.08),
    inset 0 1px 0 rgba(255, 255, 255, 0.06);
}

/* ===== Card Top Glow Bar ===== */
.card-top-glow {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  border-radius: var(--radius-lg) var(--radius-lg) 0 0;
  background: linear-gradient(90deg, #6366f1, #a855f7, #6366f1);
  background-size: 200% 100%;
  animation: shimmer 3s ease-in-out infinite;
}

@keyframes shimmer {
  0%, 100% { background-position: 0% 50%; }
  50% { background-position: 100% 50%; }
}

/* ===== Slide Up Animation ===== */
@keyframes slideUp {
  from {
    opacity: 0;
    transform: translateY(30px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

/* ===== Brand Title ===== */
.login-title {
  font-size: 2rem;
  font-weight: 700;
  text-align: center;
  background: var(--brand-gradient);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
  margin-bottom: 0.25rem;
  filter: drop-shadow(0 0 12px rgba(99, 102, 241, 0.3));
}

.login-tagline {
  text-align: center;
  color: var(--text-tertiary);
  font-size: 0.8125rem;
  letter-spacing: 0.05em;
  margin-bottom: 0.25rem;
}

.login-subtitle {
  text-align: center;
  color: var(--text-secondary);
  font-size: 0.875rem;
  margin-bottom: 1.5rem;
}

/* ===== Form ===== */
.login-form {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.form-group {
  display: flex;
  flex-direction: column;
  gap: 0.375rem;
}

.form-group label {
  font-size: 0.8125rem;
  font-weight: 500;
  color: var(--text-secondary);
}

.form-group .optional {
  font-weight: 400;
  color: var(--text-tertiary);
}

.form-group input {
  padding: 0.5rem 0.75rem;
  border: 1px solid var(--border-default);
  border-radius: var(--radius-sm);
  font-size: 0.875rem;
  outline: none;
  background: var(--bg-surface);
  color: var(--text-primary);
  transition: border-color 0.2s ease, box-shadow 0.2s ease;
}

.form-group input::placeholder {
  color: var(--text-secondary);
}

.form-group input:focus {
  border-color: transparent;
  box-shadow:
    0 0 0 1px var(--brand-primary),
    0 0 0 3px rgba(99, 102, 241, 0.15),
    0 0 12px rgba(99, 102, 241, 0.1);
}

/* ===== Messages ===== */
.error-message {
  font-size: 0.8125rem;
  color: var(--color-error);
  text-align: center;
}

.success-message {
  font-size: 0.8125rem;
  color: var(--color-success);
  text-align: center;
}

/* ===== Submit Button ===== */
.btn-submit {
  padding: 0.625rem;
  background: var(--brand-gradient);
  color: #fff;
  border: none;
  border-radius: var(--radius-sm);
  font-size: 0.875rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s ease;
  margin-top: 0.25rem;
  position: relative;
  overflow: hidden;
}

.btn-submit:hover:not(:disabled) {
  background: var(--brand-gradient-hover);
  box-shadow:
    0 4px 16px rgba(99, 102, 241, 0.35),
    0 0 24px rgba(99, 102, 241, 0.15);
  transform: translateY(-1px);
}

.btn-submit:active:not(:disabled) {
  transform: scale(0.97);
  box-shadow: 0 2px 8px rgba(99, 102, 241, 0.2);
}

.btn-submit:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

/* ===== Toggle Mode ===== */
.toggle-mode {
  text-align: center;
  margin-top: 1.25rem;
  font-size: 0.8125rem;
  color: var(--text-secondary);
}

.toggle-mode a {
  color: var(--brand-primary);
  text-decoration: none;
  font-weight: 500;
  transition: color 0.15s ease;
}

.toggle-mode a:hover {
  text-decoration: underline;
  color: var(--brand-primary-light, #818cf8);
}

/* ===== Reduced Motion ===== */
@media (prefers-reduced-motion: reduce) {
  .login-bg-orb-1,
  .login-bg-orb-2,
  .login-bg-orb-3 {
    animation: none;
  }
  .login-card {
    animation: none;
  }
  .card-top-glow {
    animation: none;
  }
}
</style>
