import type { Directive, DirectiveBinding } from 'vue'

/**
 * v-fade-in: 元素进入视口时淡入
 */
export const vFadeIn: Directive = {
  mounted(el: HTMLElement) {
    el.style.opacity = '0'
    el.style.transform = 'translateY(20px)'
    el.style.transition = 'opacity 0.6s ease-out, transform 0.6s ease-out'

    const observer = new IntersectionObserver(([entry]) => {
      if (entry.isIntersecting) {
        el.style.opacity = '1'
        el.style.transform = 'translateY(0)'
        observer.unobserve(el)
      }
    }, { threshold: 0.1 })
    observer.observe(el)
    ;(el as any).__observer = observer
  },
  unmounted(el: HTMLElement) {
    ;(el as any).__observer?.disconnect()
  }
}

/**
 * v-stagger: 列表子元素交错入场
 */
export const vStagger: Directive = {
  mounted(el: HTMLElement, binding: DirectiveBinding) {
    const delay = binding.value || 80
    const children = el.children

    Array.from(children).forEach((child, i) => {
      const htmlChild = child as HTMLElement
      htmlChild.style.opacity = '0'
      htmlChild.style.transform = 'translateY(12px)'
      htmlChild.style.transition = `opacity 0.4s ease-out ${i * delay}ms, transform 0.4s ease-out ${i * delay}ms`
    })

    const observer = new IntersectionObserver(([entry]) => {
      if (entry.isIntersecting) {
        Array.from(children).forEach((child) => {
          const htmlChild = child as HTMLElement
          htmlChild.style.opacity = '1'
          htmlChild.style.transform = 'translateY(0)'
        })
        observer.unobserve(el)
      }
    }, { threshold: 0.1 })
    observer.observe(el)
    ;(el as any).__observer = observer
  },
  unmounted(el: HTMLElement) {
    ;(el as any).__observer?.disconnect()
  }
}
