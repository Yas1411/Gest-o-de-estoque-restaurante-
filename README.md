# 🍣 TCC – Sistema de Gestão para Restaurante

Aplicação web em **Django** para ajudar restaurantes (com foco em culinária japonesa) a
controlar estoque, cardápio, pedidos, finanças e a segurança dos alimentos, integrando um
**ESP32** que monitora a temperatura em tempo real.

## Funcionalidades
- **Estoque:** produtos com validade, quantidade mínima/máxima, histórico de movimentações
  (entrada, saída, perda e ajuste) e alertas de reposição.
- **Cardápio e ficha técnica:** pratos com ingredientes e quantidades; a venda de um prato
  dá baixa automática nos insumos.
- **Pedidos por QR code:** o cliente acessa o cardápio pelo celular, sem login, e faz o
  pedido da mesa; o restaurante confirma, cancela e entrega pelo painel.
- **Reservas de mesa** e controle de pagamento.
- **Financeiro:** fornecedores, despesas fixas (aluguel, água, energia) e visão geral.
- **Monitoramento de temperatura:** ESP32 com sensores DS18B20 (freezer) e DHT22 (estoque
  seco) envia leituras via HTTPS à API do sistema, com LEDs de status e alertas no painel.
- **Notificações** e painel administrativo de usuários.

## Segurança
Transações com bloqueio de linha para evitar estoque negativo, limite de requisições,
tokens assinados nos QR codes e um token por sensor.

## Tecnologias
Python · Django 5 · PostgreSQL (produção) / MySQL (desenvolvimento) · Gunicorn · WhiteNoise ·
HTML/CSS · ESP32 (Arduino C++) · Deploy no Render

## Estrutura
```
├── teste/                      # projeto Django (manage.py, restaurante_app/, requirements.txt)
└── esp32/
    └── esp32_temperatura_django/   # firmware do ESP32 (DHT22 + DS18B20)
```

## Como rodar o site
1. `cd teste`
2. `pip install -r requirements.txt` (para MySQL local, também `requirements-dev.txt`)
3. Copie `.env.example` para `.env` e preencha as variáveis
4. `python manage.py migrate`
5. *(opcional)* `python manage.py seed_estoque_japones <usuario>` para criar o estoque base
6. `python manage.py runserver`

## Como configurar o ESP32
1. Abra `esp32/esp32_temperatura_django/` na Arduino IDE
2. Copie `secrets.example.h` para `secrets.h` e preencha Wi-Fi (2,4 GHz), URL do site e os
   tokens dos sensores (Django Admin → *Sensores ESP32*)
3. Instale as bibliotecas `DHT sensor library`, `OneWire` e `DallasTemperature` e envie o sketch

> O arquivo `secrets.h` está no `.gitignore` e não deve ser enviado ao repositório.
