<script setup lang="ts">
import { computed } from 'vue'
import VChart from 'vue-echarts'
import { use } from 'echarts/core'
import { CanvasRenderer } from 'echarts/renderers'
import { BarChart } from 'echarts/charts'
import { GridComponent, TooltipComponent, LegendComponent } from 'echarts/components'

use([CanvasRenderer, BarChart, GridComponent, TooltipComponent, LegendComponent])

interface BreakdownItem {
  date: string
  input: number
  output: number
  cached: number
}

interface Props {
  data: BreakdownItem[]
}

const props = defineProps<Props>()

const chartOption = computed(() => ({
  backgroundColor: 'transparent',
  tooltip: {
    trigger: 'axis',
    axisPointer: { type: 'shadow' },
    backgroundColor: 'rgba(17, 24, 39, 0.95)',
    borderColor: 'rgba(255, 255, 255, 0.1)',
    textStyle: { color: '#f1f5f9', fontSize: 12 }
  },
  legend: {
    data: ['Input Tokens', 'Output Tokens', 'Cached Tokens'],
    top: 10,
    textStyle: { color: '#94a3b8', fontSize: 12 }
  },
  grid: {
    left: '3%',
    right: '4%',
    bottom: '3%',
    top: '15%',
    containLabel: true
  },
  xAxis: {
    type: 'category',
    data: props.data.map(d => d.date),
    axisLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.1)' } },
    axisLabel: { color: '#94a3b8', fontSize: 11 }
  },
  yAxis: {
    type: 'value',
    axisLine: { show: false },
    axisLabel: { color: '#94a3b8', fontSize: 11 },
    splitLine: { lineStyle: { color: 'rgba(255, 255, 255, 0.05)' } }
  },
  series: [
    {
      name: 'Input Tokens',
      type: 'bar',
      stack: 'total',
      data: props.data.map(d => d.input),
      itemStyle: { color: '#6366f1', borderRadius: [0, 0, 0, 0] }
    },
    {
      name: 'Output Tokens',
      type: 'bar',
      stack: 'total',
      data: props.data.map(d => d.output),
      itemStyle: { color: '#8b5cf6' }
    },
    {
      name: 'Cached Tokens',
      type: 'bar',
      stack: 'total',
      data: props.data.map(d => d.cached),
      itemStyle: { color: '#10b981', borderRadius: [4, 4, 0, 0] }
    }
  ]
}))
</script>

<template>
  <div class="token-breakdown">
    <VChart :option="chartOption" autoresize style="height: 300px;" />
  </div>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.token-breakdown {
  width: 100%;
}
</style>
