# HAWK — Travessia Inteligente e Acessível

<p align="center">
  **Tecnologia e acessibilidade para uma mobilidade urbana mais inclusiva.**
</p>

---

## Sobre o projeto

O **HAWK** é um projeto desenvolvido para a Global Solution 2026.2 da FIAP, com foco em Edge Computing e CitizenTech.

A proposta consiste em desenvolver um sistema inteligente de semáforos capaz de identificar pessoas com mobilidade reduzida durante a travessia de pedestres e adaptar automaticamente o tempo de sinalização, proporcionando uma travessia mais segura e acessível.

Por meio de sensores, processamento na borda (Edge Computing) e comunicação IoT, o sistema identifica uma possível necessidade de tempo adicional e prolonga o sinal verde para pedestres enquanto a pessoa ainda estiver atravessando.

**Nosso objetivo é simples: tornar a mobilidade urbana mais inclusiva, utilizando a tecnologia para adaptar a cidade às necessidades das pessoas.**

## O problema

Os semáforos convencionais trabalham com tempos de travessia previamente estabelecidos, sem considerar as diferentes necessidades de mobilidade dos pedestres.

Para pessoas com deficiência física, idosos e indivíduos com mobilidade reduzida, esse tempo pode ser insuficiente, gerando insegurança e dificuldades durante a travessia.

O HAWK propõe uma alternativa inteligente: um sistema que reconhece a necessidade de tempo adicional e adapta o funcionamento do semáforo de acordo com a situação.

## A solução

O sistema utiliza um ESP32 responsável pelo processamento local das informações e pelo controle dos semáforos.

Quando uma pessoa com mobilidade reduzida é detectada, o sistema:

1. Analisa a confiança da detecção recebida.
2. Confirma se a detecção é válida.
3. Altera a sinalização para interromper o fluxo de veículos.
4. Libera a travessia de pedestres.
5. Monitora se a pessoa ainda está atravessando.
6. Adiciona períodos extras de travessia quando necessário.
7. Libera novamente o trânsito após a conclusão da travessia.

O sistema também possui um modo seguro para situações de falha nos sensores.

## Tecnologias utilizadas

| Tecnologia | Aplicação |
|---|---|
| ESP32 | Processamento e controle do sistema |
| C++ / Arduino | Desenvolvimento do firmware |
| MQTT | Comunicação entre dispositivos |
| HiveMQ | Broker para troca de mensagens |
| HTML | Estrutura do dashboard |
| CSS | Interface visual |
| JavaScript | Atualização dos dados em tempo real |
| Wokwi | Simulação do circuito eletrônico |
| FIWARE Ultralight | Integração IoT opcional |

## Arquitetura do sistema

O projeto é dividido em três camadas principais:

### 1. Device — IoT

Responsável pela leitura das informações e pelo controle físico do semáforo.

Componentes:
- ESP32;
- Potenciômetro para simular a confiança da detecção;
- LEDs para sinalização de veículos e pedestres;
- Buzzer para alertas sonoros;
- Display LCD 16x2;
- Botão para simulação de falhas.

### 2. Comunicação — Back-end IoT

Responsável pela comunicação entre o dispositivo e o dashboard.

Utiliza o protocolo MQTT por meio do broker HiveMQ, permitindo a publicação e o recebimento de informações em tempo real.

Também possui suporte opcional à publicação de dados no formato FIWARE Ultralight.

### 3. Dashboard — Monitoramento

Interface web responsável por apresentar as informações do sistema.

Permite acompanhar:
- Estado atual do semáforo;
- Sinalização de veículos e pedestres;
- Confiança da detecção;
- Origem dos dados recebidos;
- Quantidade de travessias realizadas;
- Tempo médio de espera;
- Tempo médio de travessia;
- Extensões de tempo;
- Registro de falhas;
- Histórico de eventos.

## Funcionamento do sistema

O HAWK trabalha com cinco estados:

| Estado | Funcionamento |
|---|---|
| NORMAL | Semáforo opera normalmente. |
| DETECTADO | Uma pessoa com mobilidade reduzida foi identificada. |
| TRAVESSIA | Sinal verde liberado para pedestres. |
| LIBERACAO | Finalização da travessia e preparação para liberar os veículos. |
| FALHA | Sistema entra em modo seguro, garantindo um ciclo de travessia. |

### Travessia adaptativa

O diferencial do HAWK está na capacidade de adaptar o tempo de travessia.

- **70%:** confiança mínima para validar uma detecção.
- **1,5 segundo:** período necessário para confirmar a detecção.
- **8 segundos:** tempo mínimo de travessia.
- **3 segundos:** acréscimo por extensão.
- **20 segundos:** limite máximo da travessia.
- **1 segundo:** período sem detecção para considerar a faixa livre.

Esses parâmetros fazem parte da lógica implementada no protótipo e podem ser ajustados conforme testes e validações.

### Modo seguro

Caso sejam identificadas três leituras inválidas consecutivas ou seja acionado o botão de falha, o sistema entra automaticamente no estado FALHA.

Nesse modo, o semáforo utiliza um ciclo fixo, garantindo um período de travessia para pedestres mesmo diante de problemas na detecção.

## Componentes e conexões

| Componente | Pino ESP32 |
|---|---|
| Potenciômetro | GPIO 34 |
| Botão de falha | GPIO 4 |
| Buzzer | GPIO 23 |
| LED vermelho (veículos) | GPIO 25 |
| LED amarelo (veículos) | GPIO 26 |
| LED verde (veículos) | GPIO 27 |
| LED vermelho (pedestres) | GPIO 32 |
| LED verde (pedestres) | GPIO 33 |
| LCD SDA | GPIO 21 |
| LCD SCL | GPIO 22 |

**Observações:**
- LEDs devem utilizar resistores de 330 Ω.
- LCD I2C utiliza endereço `0x27`.
- Alimentação do LCD: 5V.
- O ESP32 utiliza alimentação de 3,3V para o potenciômetro.

## Como executar o projeto

### 1. Acessar o repositório

Clone o projeto:

```bash
git clone https://github.com/HAWK-GS02/HAWK-cross.git
```

Entre na pasta:

```bash
cd HAWK-cross
```

### 2. Configurar a simulação

1. Acesse o [Wokwi](https://wokwi.com/).
2. Crie um novo projeto utilizando ESP32.
3. Abra os arquivos correspondentes no projeto.
4. Copie o conteúdo de `sketch.ino`, `diagram.json` e `libraries.txt` para as respectivas abas.
5. Execute a simulação pelo botão de iniciar.

### 3. Executar o dashboard

Com a simulação em funcionamento:

1. Abra o arquivo `dashboard.html` em um navegador.
2. Aguarde a conexão com o broker MQTT.
3. Observe as informações recebidas do ESP32.

O dashboard utiliza comunicação MQTT via WebSocket para receber as atualizações do dispositivo.

## Testando o sistema

É possível simular diferentes situações durante a execução.

| Teste | Ação |
|---|---|
| Operação normal | Manter o potenciômetro em um valor baixo. |
| Detecção | Elevar o potenciômetro acima de 70%. |
| Confirmação | Manter o valor elevado por pelo menos 1,5 segundo. |
| Travessia | Observar a mudança dos sinais e o acionamento do buzzer. |
| Extensão | Manter o valor elevado durante a travessia. |
| Liberação | Reduzir o valor para simular a faixa livre. |
| Falha | Acionar o botão azul. |

O dashboard também possui botões para simular o envio de informações de uma câmera, permitindo testar a comunicação MQTT sem depender de um sistema de visão computacional real.

## Estrutura do repositório

```text
HAWK-cross/
│
├── sketch.ino       # Firmware do ESP32
├── diagram.json     # Circuito eletrônico
├── libraries.txt    # Bibliotecas utilizadas
├── dashboard.html   # Dashboard de monitoramento
├── video.txt        # Arquivo de referência do vídeo
└── Readme.md        # Documentação do projeto
```

## Diferenciais do projeto

- **Acessibilidade:** adaptação do tempo de travessia às necessidades dos pedestres.
- **Edge Computing:** processamento local das decisões do semáforo.
- **IoT:** comunicação em tempo real entre dispositivos.
- **Segurança:** modo de operação alternativo em situações de falha.
- **Monitoramento:** dashboard para acompanhamento dos indicadores.
- **Adaptabilidade:** extensão automática do tempo enquanto a travessia não é concluída.

## Contexto acadêmico

Projeto desenvolvido como parte da **Global Solution 2026.2 — FIAP**, dentro da proposta de Edge Computing e CitizenTech.

A iniciativa busca demonstrar como a integração entre Internet das Coisas, processamento na borda e acessibilidade pode contribuir para a construção de cidades mais inteligentes e inclusivas.

## Equipe HAWK

| Integrante | RM |
|---|---:|
| Esther dos Santos de Almeida Tozzo | 570860 |
| Felipe de Oliveira Zimmermann | 570863 |
| Izabela Pordeus de Almeida | 570316 |
| João Victor Santos Souza | 569949 |
| Matheus Lopes Lima | 571458 |

---

<p align="center">
  **HAWK — A cidade que se adapta a você.**
  <br/>
  FIAP • Global Solution 2026.2
</p>
