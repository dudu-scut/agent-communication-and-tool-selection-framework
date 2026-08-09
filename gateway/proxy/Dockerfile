FROM node:22-alpine

WORKDIR /app/gateway/proxy
COPY gateway/proxy/package.json gateway/proxy/package-lock.json ./
RUN npm ci --omit=dev
COPY gateway/proxy/server.mjs ./server.mjs
COPY proto /app/proto

EXPOSE 8081
CMD ["node", "server.mjs"]
