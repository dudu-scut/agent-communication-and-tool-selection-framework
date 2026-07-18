import { createApp } from 'vue'
import { createPinia } from 'pinia'
import router from './router'
import App from './App.vue'
import './styles/global.css'
import { vFadeIn, vStagger } from './directives/animations'

const app = createApp(App)

app.use(createPinia())
app.use(router)

// Register custom directives
app.directive('fade-in', vFadeIn)
app.directive('stagger', vStagger)

app.mount('#app')
