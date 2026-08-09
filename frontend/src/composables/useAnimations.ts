import { ref, onMounted, onUnmounted, type Ref } from 'vue'
import { CountUp } from 'countup.js'

/**
 * Staggered entry animation for list children
 * Uses IntersectionObserver to watch the container enter the viewport,
 * then adds staggered animation-delay to each child
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
 * Count-up number animation (wraps countup.js)
 * Rolls the number from 0 to the target value when the element enters the viewport
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
 * Glow effect control
 * Adds a glowing box-shadow on mouseenter and removes it on mouseleave
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
