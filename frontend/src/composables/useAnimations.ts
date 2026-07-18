import { ref, onMounted, onUnmounted, type Ref } from 'vue'
import { CountUp } from 'countup.js'

/**
 * 列表项交错入场动画
 * 使用 IntersectionObserver 监听容器进入视口，
 * 进入后为每个子元素添加交错的 animation-delay
 */
export function useStaggerAnimation(containerRef: Ref<HTMLElement | null>, delay = 80) {
  const isVisible = ref(false)
  let observer: IntersectionObserver | null = null

  onMounted(() => {
    if (!containerRef.value) return

    observer = new IntersectionObserver(([entry]) => {
      if (entry.isIntersecting) {
        isVisible.value = true
        const children = containerRef.value?.children
        if (children) {
          Array.from(children).forEach((child, i) => {
            const el = child as HTMLElement
            el.style.opacity = '0'
            el.style.transform = 'translateY(12px)'
            el.style.transition = `opacity 0.4s ease-out ${i * delay}ms, transform 0.4s ease-out ${i * delay}ms`
            // Force reflow then animate
            requestAnimationFrame(() => {
              el.style.opacity = '1'
              el.style.transform = 'translateY(0)'
            })
          })
        }
        observer?.unobserve(containerRef.value!)
      }
    }, { threshold: 0.1 })

    observer.observe(containerRef.value)
  })

  onUnmounted(() => {
    observer?.disconnect()
  })

  return { isVisible }
}

/**
 * 数字滚动动画（封装 countup.js）
 * 数字从0滚动到目标值，在元素进入视口时启动
 */
export function useCountUp(
  targetRef: Ref<HTMLElement | null>,
  endValue: number,
  options?: { duration?: number; prefix?: string; suffix?: string; separator?: boolean }
) {
  const isAnimating = ref(false)
  let countUpInstance: CountUp | null = null
  let observer: IntersectionObserver | null = null

  const start = () => {
    if (!targetRef.value) return
    countUpInstance = new CountUp(targetRef.value, endValue, {
      duration: options?.duration ?? 2,
      prefix: options?.prefix ?? '',
      suffix: options?.suffix ?? '',
      useGrouping: options?.separator ?? true,
      enableScrollSpy: false,
    })
    if (countUpInstance.error) {
      console.error(countUpInstance.error)
      return
    }
    isAnimating.value = true
    countUpInstance.start(() => {
      isAnimating.value = false
    })
  }

  onMounted(() => {
    if (!targetRef.value) return

    observer = new IntersectionObserver(([entry]) => {
      if (entry.isIntersecting) {
        start()
        observer?.unobserve(targetRef.value!)
      }
    }, { threshold: 0.1 })

    observer.observe(targetRef.value)
  })

  onUnmounted(() => {
    observer?.disconnect()
  })

  return { isAnimating, start }
}

/**
 * 发光效果控制
 * mouseenter 时添加发光 box-shadow，mouseleave 时移除
 */
export function useGlowEffect(elementRef: Ref<HTMLElement | null>, color = 'rgba(99, 102, 241, 0.4)') {
  const isHovered = ref(false)

  const handleMouseEnter = () => {
    isHovered.value = true
    if (elementRef.value) {
      elementRef.value.style.boxShadow = `0 0 20px ${color}`
      elementRef.value.style.transition = 'box-shadow 0.3s ease-out'
    }
  }

  const handleMouseLeave = () => {
    isHovered.value = false
    if (elementRef.value) {
      elementRef.value.style.boxShadow = 'none'
    }
  }

  onMounted(() => {
    if (elementRef.value) {
      elementRef.value.addEventListener('mouseenter', handleMouseEnter)
      elementRef.value.addEventListener('mouseleave', handleMouseLeave)
    }
  })

  onUnmounted(() => {
    if (elementRef.value) {
      elementRef.value.removeEventListener('mouseenter', handleMouseEnter)
      elementRef.value.removeEventListener('mouseleave', handleMouseLeave)
    }
  })

  return { isHovered }
}
