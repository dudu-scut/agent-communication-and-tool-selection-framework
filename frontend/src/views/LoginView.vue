<template>
  <div class="login-view">
    <div class="login-card">
      <h1 class="login-title">NexusAI</h1>
      <p class="login-subtitle">{{ isRegister ? '创建新账户' : '登录以继续' }}</p>

      <form class="login-form" @submit.prevent="handleSubmit">
        <div class="form-group">
          <label for="username">用户名</label>
          <input
            id="username"
            v-model="form.username"
            type="text"
            placeholder="输入用户名"
            autocomplete="username"
            required
          />
        </div>

        <div v-if="isRegister" class="form-group">
          <label for="displayName">显示名称 <span class="optional">(可选)</span></label>
          <input
            id="displayName"
            v-model="form.displayName"
            type="text"
            placeholder="输入显示名称"
            autocomplete="name"
          />
        </div>

        <div class="form-group">
          <label for="password">密码</label>
          <input
            id="password"
            v-model="form.password"
            type="password"
            placeholder="输入密码"
            autocomplete="current-password"
            required
          />
        </div>

        <p v-if="error" class="error-message">{{ error }}</p>

        <button type="submit" class="btn-submit" :disabled="loading">
          {{ loading ? '请稍候...' : (isRegister ? '注册' : '登录') }}
        </button>
      </form>

      <p class="toggle-mode">
        <template v-if="isRegister">
          已有账户？<a href="#" @click.prevent="switchToLogin">去登录</a>
        </template>
        <template v-else>
          没有账户？<a href="#" @click.prevent="switchToRegister">去注册</a>
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

const form = reactive({
  username: '',
  password: '',
  displayName: '',
})

function switchToLogin() {
  isRegister.value = false
  error.value = ''
}

function switchToRegister() {
  isRegister.value = true
  error.value = ''
}

async function handleSubmit() {
  if (!form.username.trim() || !form.password.trim()) {
    error.value = '请填写用户名和密码'
    return
  }

  loading.value = true
  error.value = ''

  try {
    if (isRegister.value) {
      const err = await auth.register(form.username.trim(), form.password, form.displayName.trim())
      if (err) {
        error.value = err
        return
      }
      // Registration successful — switch to login
      isRegister.value = false
      error.value = ''
      form.password = ''
      // Show success message via error field (reuse UI)
      error.value = '注册成功，请登录'
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
.login-view {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  background: #f9fafb;
  padding: 1rem;
}

.login-card {
  background: #fff;
  border-radius: 12px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.08), 0 1px 2px rgba(0, 0, 0, 0.06);
  padding: 2.5rem 2rem;
  width: 100%;
  max-width: 380px;
}

.login-title {
  font-size: 1.75rem;
  font-weight: 700;
  text-align: center;
  color: #111827;
  margin-bottom: 0.25rem;
}

.login-subtitle {
  text-align: center;
  color: #6b7280;
  font-size: 0.875rem;
  margin-bottom: 1.5rem;
}

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
  color: #374151;
}

.form-group .optional {
  font-weight: 400;
  color: #9ca3af;
}

.form-group input {
  padding: 0.5rem 0.75rem;
  border: 1px solid #d1d5db;
  border-radius: 6px;
  font-size: 0.875rem;
  outline: none;
  transition: border-color 0.15s;
}

.form-group input:focus {
  border-color: #3b82f6;
  box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.15);
}

.error-message {
  font-size: 0.8125rem;
  color: #dc2626;
  text-align: center;
}

.btn-submit {
  padding: 0.625rem;
  background: #3b82f6;
  color: #fff;
  border: none;
  border-radius: 6px;
  font-size: 0.875rem;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.15s;
  margin-top: 0.25rem;
}

.btn-submit:hover:not(:disabled) {
  background: #2563eb;
}

.btn-submit:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.toggle-mode {
  text-align: center;
  margin-top: 1.25rem;
  font-size: 0.8125rem;
  color: #6b7280;
}

.toggle-mode a {
  color: #3b82f6;
  text-decoration: none;
  font-weight: 500;
}

.toggle-mode a:hover {
  text-decoration: underline;
}
</style>
