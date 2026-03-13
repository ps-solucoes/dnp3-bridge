# Guia de Integração gRPC — Aplicação Python com o dnp3-bridge

Este documento é destinado ao desenvolvimento da aplicação Python de produção que irá se comunicar
com o serviço C++ `dnp3-bridge` via gRPC. Ele explica em detalhe o protocolo, os tipos de mensagem,
os fluxos de dados bidirecionais e como implementar cada interação corretamente.

> **Referência de código:** O simulador em `tools/python-dsp-sim/` é uma implementação funcional
> completa de todos os fluxos descritos aqui. Use-o como referência de código.

---

## Índice

1. [Visão geral da arquitetura](#1-visão-geral-da-arquitetura)
2. [Conexão gRPC](#2-conexão-grpc)
3. [Protocolo — mensagens e enums](#3-protocolo--mensagens-e-enums)
4. [Fluxo 1: Enviar dados de pontos (Python → SCADA)](#4-fluxo-1-enviar-dados-de-pontos-python--scada)
5. [Fluxo 2: Receber comandos do SCADA (SCADA → Python)](#5-fluxo-2-receber-comandos-do-scada-scada--python)
6. [Fluxo 3: Consultar status da outstation](#6-fluxo-3-consultar-status-da-outstation)
7. [Mapa de pontos DNP3](#7-mapa-de-pontos-dnp3)
8. [Tratamento de erros e reconexão](#8-tratamento-de-erros-e-reconexão)
9. [Threading e concorrência](#9-threading-e-concorrência)
10. [Exemplos completos em Python](#10-exemplos-completos-em-python)
11. [Checklist de integração](#11-checklist-de-integração)

---

## 1. Visão geral da arquitetura

```
DSP  <==Modbus==>  Python (gRPC client)  <==gRPC==>  C++ dnp3-bridge  <==DNP3==>  SCADA
```

A aplicação Python é o **cliente gRPC**. O `dnp3-bridge` é o **servidor gRPC** e a **outstation DNP3**.
O bridge é intencionalmente fino — apenas traduz protocolos. Toda a lógica de negócio,
análise de dados e decisões operacionais ficam no lado Python.

### Responsabilidades do Python

| Responsabilidade | Descrição |
|---|---|
| Ler dados do DSP via Modbus | Coletar medições e status do equipamento |
| Enviar pontos ao bridge | RPC `UpdatePoints` com valores atualizados |
| Escutar comandos SCADA | Stream `StreamCommands` (conexão persistente) |
| Processar comandos | Converter comando DNP3 → ação Modbus no DSP |
| Responder ao SCADA | RPC `RespondToCommand` com resultado |
| Monitorar status | RPC `GetStatus` para verificar conexão |

### Direção dos dados

```
┌─────────────────────────────────────────────────────────────────┐
│                    PYTHON → SCADA (pontos)                      │
│                                                                 │
│  Python lê Modbus → monta UpdateRequest → chama UpdatePoints    │
│  → bridge enfileira → aplica na outstation DNP3 → SCADA lê     │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    SCADA → PYTHON (comandos)                    │
│                                                                 │
│  SCADA envia comando DNP3 → outstation recebe → bridge monta   │
│  CommandRequest → envia pelo stream → Python recebe →           │
│  Python executa Modbus → Python chama RespondToCommand →        │
│  bridge retorna resultado ao SCADA                              │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Conexão gRPC

### Setup básico

```python
import grpc
from generated import dnp3bridge_pb2 as pb
from generated import dnp3bridge_pb2_grpc as pb_grpc

# Criar canal e stub
channel = grpc.insecure_channel("localhost:50051")
stub = pb_grpc.BridgeServiceStub(channel)
```

O endereço padrão é `0.0.0.0:50051` (configurável via `DNP3_BRIDGE_GRPC_ADDRESS`).
Em produção no BeagleBone, a aplicação Python e o bridge rodam na mesma máquina,
então `localhost:50051` é o endereço correto.

### Gerar os stubs Python

```bash
cd tools/python-dsp-sim
./generate_proto.sh
```

Isto gera `generated/dnp3bridge_pb2.py` e `generated/dnp3bridge_pb2_grpc.py`
a partir de `proto/dnp3bridge.proto`. Copie o diretório `generated/` para seu projeto
ou regenere com o mesmo script apontando para o `.proto`.

### Dependências

```
grpcio>=1.60.0
grpcio-tools>=1.60.0
```

---

## 3. Protocolo — mensagens e enums

O arquivo `proto/dnp3bridge.proto` define tudo. Abaixo está a referência completa.

### Serviço `BridgeService`

| RPC | Tipo | Direção | Descrição |
|---|---|---|---|
| `UpdatePoints` | Unário | Python → Bridge | Envia atualizações de pontos (analógicos, binários, contadores) |
| `GetStatus` | Unário | Python → Bridge | Consulta estado da outstation e timestamp da última atualização |
| `StreamCommands` | Server-streaming | Bridge → Python | Stream persistente por onde chegam comandos do SCADA |
| `RespondToCommand` | Unário | Python → Bridge | Envia resposta de um comando processado |

### Enums

#### `PointQuality` — qualidade do ponto

| Valor | Nome | Uso |
|---|---|---|
| 0 | `POINT_QUALITY_GOOD` | Medição válida e confiável |
| 1 | `POINT_QUALITY_UNCERTAIN` | Medição com incerteza (sensor degradado, etc.) |
| 2 | `POINT_QUALITY_BAD` | Medição inválida (falha de comunicação, etc.) |
| 3 | `POINT_QUALITY_RESTART` | Ponto em estado de restart (ainda não atualizado) |

> **Importante:** Use `POINT_QUALITY_GOOD` para medições normais. Se a comunicação Modbus
> com o DSP falhar, envie os pontos com `POINT_QUALITY_BAD` para que o SCADA saiba que
> os valores não são confiáveis. O bridge converte a qualidade para flags DNP3 correspondentes.

#### `CommandType` — tipo de comando do SCADA

| Valor | Nome | Uso no sistema |
|---|---|---|
| 0 | `COMMAND_TYPE_UNSPECIFIED` | Não usado |
| 1 | `COMMAND_TYPE_CROB` | Control Relay Output Block — comando binário (ligar/desligar) |
| 2 | `COMMAND_TYPE_ANALOG_INT16` | Setpoint analógico inteiro 16-bit |
| 3 | `COMMAND_TYPE_ANALOG_INT32` | Setpoint analógico inteiro 32-bit |
| 4 | `COMMAND_TYPE_ANALOG_FLOAT32` | Setpoint analógico float 32-bit (**mais comum**) |
| 5 | `COMMAND_TYPE_ANALOG_DOUBLE64` | Setpoint analógico double 64-bit |

> Na prática, o SCADA do sistema usa `COMMAND_TYPE_CROB` para os 20 Binary Outputs
> e `COMMAND_TYPE_ANALOG_FLOAT32` para os 5 Analog Outputs. Os outros tipos analógicos
> são suportados mas improvável que sejam usados.

#### `CrobOperationType` — tipo de operação CROB

| Valor | Nome | Significado |
|---|---|---|
| 0 | `CROB_OPERATION_NUL` | Nulo (sem ação) |
| 1 | `CROB_OPERATION_PULSE_ON` | Pulso de ativação (temporário) |
| 2 | `CROB_OPERATION_PULSE_OFF` | Pulso de desativação (temporário) |
| 3 | `CROB_OPERATION_LATCH_ON` | Latch ON (mantém estado ligado) |
| 4 | `CROB_OPERATION_LATCH_OFF` | Latch OFF (mantém estado desligado) |

> **Na prática:** O SCADA geralmente usa `LATCH_ON` / `LATCH_OFF` para comandos
> de ligar/desligar (ex: "Conectar Equipamento" = LATCH_ON no ponto 0).
> `PULSE_ON` / `PULSE_OFF` são usados para ações momentâneas
> (ex: "Reset Proteção" = PULSE_ON no ponto 2).

#### `CommandResultStatus` — resultado do comando

| Valor | Nome | Quando usar |
|---|---|---|
| 0 | `COMMAND_RESULT_SUCCESS` | Comando executado com sucesso no DSP |
| 1 | `COMMAND_RESULT_TIMEOUT` | DSP não respondeu a tempo (timeout Modbus) |
| 4 | `COMMAND_RESULT_NOT_SUPPORTED` | Comando não reconhecido ou não implementado |
| 6 | `COMMAND_RESULT_HARDWARE_ERROR` | Erro de hardware no DSP |
| 7 | `COMMAND_RESULT_LOCAL` | Equipamento em modo local, não aceita comandos remotos |
| 10 | `COMMAND_RESULT_DOWNSTREAM_FAIL` | Falha de comunicação com o DSP |

> Os demais status (`NO_SELECT`, `FORMAT_ERROR`, `ALREADY_ACTIVE`, `TOO_MANY_OPS`,
> `NOT_AUTHORIZED`) são menos comuns mas estão disponíveis se necessário.

### Mensagens

#### Mensagens de pontos

```protobuf
message AnalogPoint {
    uint32       index   = 1;   // Índice do ponto (0-20 para AI, 0-4 para AO)
    double       value   = 2;   // Valor da medição
    PointQuality quality = 3;   // Qualidade do ponto
}

message BinaryPoint {
    uint32       index   = 1;   // Índice do ponto (0-10 para BI)
    bool         value   = 2;   // true = 1 (ativo), false = 0 (inativo)
    PointQuality quality = 3;   // Qualidade do ponto
}

message CounterPoint {
    uint32       index   = 1;   // Índice do ponto
    uint32       value   = 2;   // Valor do contador
    PointQuality quality = 3;   // Qualidade do ponto
}
```

#### Mensagem de atualização

```protobuf
message UpdateRequest {
    repeated AnalogPoint  analogs  = 1;   // Lista de analógicos a atualizar
    repeated BinaryPoint  binaries = 2;   // Lista de binários a atualizar
    repeated CounterPoint counters = 3;   // Lista de contadores a atualizar
}

message UpdateResponse {
    bool   success = 1;
    string message = 2;
}
```

> Você pode enviar qualquer combinação de pontos em um único `UpdateRequest`.
> Não é necessário enviar todos os pontos de uma vez — envie apenas os que mudaram.

#### Mensagens de comando

```protobuf
message CrobCommand {
    CrobOperationType operation = 1;   // Tipo de operação
    uint32 count       = 2;           // Número de execuções (geralmente 1)
    uint32 on_time_ms  = 3;           // Tempo ON em milissegundos (para PULSE)
    uint32 off_time_ms = 4;           // Tempo OFF em milissegundos (para PULSE)
}

message CommandRequest {
    uint64      command_id   = 1;   // ID único (atribuído pelo bridge)
    uint32      point_index  = 2;   // Índice do ponto alvo
    CommandType command_type = 3;   // Tipo do comando (CROB ou analógico)
    oneof payload {
        CrobCommand crob         = 4;   // Detalhes CROB (se command_type == CROB)
        double      analog_value = 5;   // Valor analógico (se command_type == ANALOG_*)
    }
}

message CommandResponse {
    uint64              command_id = 1;   // Mesmo ID recebido no CommandRequest
    CommandResultStatus status     = 2;   // Resultado da execução
}
```

---

## 4. Fluxo 1: Enviar dados de pontos (Python → SCADA)

Este é o fluxo principal: o Python lê medições do DSP via Modbus e as envia ao bridge
para que o SCADA possa lê-las via DNP3.

### Diagrama de sequência

```
Python              Bridge (C++)           Outstation DNP3        SCADA
  │                    │                       │                    │
  │──UpdatePoints()──>│                       │                    │
  │  (analogs+binaries)│                       │                    │
  │                    │──enfileira────────>│                    │
  │                    │  (thread de flush)    │                    │
  │                    │──────────────────>│                    │
  │                    │  Apply(updates)       │                    │
  │<─UpdateResponse──│                       │                    │
  │  success=true      │                       │                    │
  │                    │                       │                    │
  │                    │                       │<──Integrity Poll──│
  │                    │                       │──responde dados──>│
```

### Como implementar

```python
def enviar_medicoes(stub, dados_modbus):
    """Envia medições lidas do DSP para o bridge."""

    # Montar pontos analógicos (medições elétricas)
    analogs = [
        pb.AnalogPoint(index=0, value=dados_modbus.tensao_a, quality=pb.POINT_QUALITY_GOOD),
        pb.AnalogPoint(index=1, value=dados_modbus.tensao_b, quality=pb.POINT_QUALITY_GOOD),
        pb.AnalogPoint(index=2, value=dados_modbus.tensao_c, quality=pb.POINT_QUALITY_GOOD),
        pb.AnalogPoint(index=3, value=dados_modbus.corrente_a, quality=pb.POINT_QUALITY_GOOD),
        # ... demais pontos analógicos (ver tabela na seção 7)
    ]

    # Montar pontos binários (status do equipamento)
    binaries = [
        pb.BinaryPoint(index=0, value=dados_modbus.energizado, quality=pb.POINT_QUALITY_GOOD),
        pb.BinaryPoint(index=1, value=dados_modbus.operando, quality=pb.POINT_QUALITY_GOOD),
        pb.BinaryPoint(index=2, value=dados_modbus.alarme, quality=pb.POINT_QUALITY_GOOD),
        # ... demais pontos binários (ver tabela na seção 7)
    ]

    request = pb.UpdateRequest(analogs=analogs, binaries=binaries)

    try:
        response = stub.UpdatePoints(request)
        if not response.success:
            log.warning("UpdatePoints retornou success=false: %s", response.message)
    except grpc.RpcError as e:
        log.error("Falha ao enviar pontos: %s", e.code())
```

### Boas práticas

- **Frequência:** Envie atualizações a cada ciclo de leitura Modbus (tipicamente 1-2s).
  Não há throttling no bridge — ele processa tudo que receber.
- **Atualizações parciais:** Você pode enviar apenas os pontos que mudaram desde a
  última leitura. O bridge atualiza individualmente cada ponto na outstation DNP3.
- **Qualidade:** Se a comunicação Modbus falhar, envie com `POINT_QUALITY_BAD` em vez
  de simplesmente não enviar. Isso informa ao SCADA que há um problema.
- **Atômicidade:** Todos os pontos em um único `UpdateRequest` são enfileirados juntos
  e aplicados no mesmo batch. Isso garante consistência (o SCADA não vê metade dos dados
  de um ciclo e metade de outro).

---

## 5. Fluxo 2: Receber comandos do SCADA (SCADA → Python)

Este fluxo é mais complexo. O SCADA envia um comando DNP3 (CROB ou setpoint analógico)
que precisa chegar ao DSP via Python. O bridge age como intermediário síncrono:
ele **bloqueia** a resposta ao SCADA até que o Python responda ou ocorra timeout.

### Diagrama de sequência

```
SCADA             Outstation DNP3         Bridge (C++)              Python
  │                    │                       │                       │
  │──CROB Operate()──>│                       │                       │
  │                    │──ForwardingCmdHandler──>│                       │
  │                    │                       │──CommandRequest──────>│
  │                    │                       │  (via StreamCommands)  │
  │                    │                       │                       │
  │                    │                       │  (bridge BLOQUEIA      │
  │                    │                       │   aguardando resposta) │
  │                    │                       │                       │
  │                    │                       │        Python executa  │
  │                    │                       │        Modbus no DSP   │
  │                    │                       │                       │
  │                    │                       │<─RespondToCommand─────│
  │                    │                       │  command_id + SUCCESS  │
  │                    │                       │                       │
  │                    │<──CommandStatus────────│                       │
  │<──DNP3 response───│                       │                       │
  │  (SUCCESS)         │                       │                       │
```

### Timeout

O bridge tem um timeout padrão de **3 segundos** (`command_timeout_ms`).
Se o Python não chamar `RespondToCommand` dentro desse tempo:

1. O bridge retorna `TIMEOUT` ao SCADA
2. O comando é removido da fila de pendentes
3. Se o Python responder depois, o `RespondToCommand` retorna `success=false`

> **Importante:** O timeout conta a partir do momento que o bridge envia o comando
> pelo stream, não de quando o SCADA enviou. Sua aplicação deve responder o mais
> rápido possível. O ciclo típico deve ser: receber comando → executar Modbus → responder
> em menos de 1 segundo.

### Como implementar

A implementação requer **duas partes** rodando em paralelo:

#### Parte 1: Listener de comandos (thread separada)

```python
def command_listener(stub):
    """
    Thread dedicada que escuta comandos do SCADA via StreamCommands.
    Esta função BLOQUEIA — deve rodar em uma thread separada.
    """
    while not shutdown_event.is_set():
        try:
            # Abre o stream (bloqueia até receber dados ou desconectar)
            stream = stub.StreamCommands(pb.StreamCommandsRequest())

            for cmd in stream:
                # cmd é um CommandRequest
                processar_comando(stub, cmd)

        except grpc.RpcError as e:
            if shutdown_event.is_set():
                break
            log.warning("Stream desconectou: %s. Reconectando em 2s...", e.code())
            time.sleep(2)
```

> **Crítico:** O `StreamCommands` é um RPC server-streaming de **longa duração**.
> Deve haver **exatamente uma** conexão ativa a qualquer momento. Se a conexão cair
> (ex: bridge reiniciou), reconecte automaticamente.

#### Parte 2: Processamento e resposta

```python
def processar_comando(stub, cmd):
    """Processa um comando recebido do SCADA e envia a resposta."""

    if cmd.command_type == pb.COMMAND_TYPE_CROB:
        processar_crob(stub, cmd)
    elif cmd.command_type in (
        pb.COMMAND_TYPE_ANALOG_FLOAT32,
        pb.COMMAND_TYPE_ANALOG_INT16,
        pb.COMMAND_TYPE_ANALOG_INT32,
        pb.COMMAND_TYPE_ANALOG_DOUBLE64,
    ):
        processar_analog(stub, cmd)
    else:
        responder(stub, cmd.command_id, pb.COMMAND_RESULT_NOT_SUPPORTED)


def processar_crob(stub, cmd):
    """
    Processa um comando CROB (binary output).

    O campo cmd.crob contém:
      - cmd.crob.operation: tipo de operação (LATCH_ON, PULSE_ON, etc.)
      - cmd.crob.count: número de execuções
      - cmd.crob.on_time_ms: tempo ON em ms (relevante para PULSE)
      - cmd.crob.off_time_ms: tempo OFF em ms (relevante para PULSE)

    O campo cmd.point_index indica qual Binary Output (0-19).
    """
    point_index = cmd.point_index
    operation = cmd.crob.operation

    log.info(
        "Comando CROB #%d: ponto=%d op=%s count=%d on=%dms off=%dms",
        cmd.command_id,
        point_index,
        pb.CrobOperationType.Name(operation),
        cmd.crob.count,
        cmd.crob.on_time_ms,
        cmd.crob.off_time_ms,
    )

    # Converter para ação Modbus e executar no DSP
    # Exemplo: LATCH_ON no ponto 0 = escrever coil 0 = True
    try:
        if operation in (pb.CROB_OPERATION_LATCH_ON, pb.CROB_OPERATION_PULSE_ON):
            # Escrever coil no endereço Modbus correspondente
            modbus_client.write_coil(point_index, True)
        elif operation in (pb.CROB_OPERATION_LATCH_OFF, pb.CROB_OPERATION_PULSE_OFF):
            modbus_client.write_coil(point_index, False)

        responder(stub, cmd.command_id, pb.COMMAND_RESULT_SUCCESS)

    except ModbusTimeoutError:
        responder(stub, cmd.command_id, pb.COMMAND_RESULT_TIMEOUT)
    except Exception:
        responder(stub, cmd.command_id, pb.COMMAND_RESULT_DOWNSTREAM_FAIL)


def processar_analog(stub, cmd):
    """
    Processa um comando de setpoint analógico.

    O campo cmd.analog_value contém o valor (float64).
    O campo cmd.point_index indica qual Analog Output (0-4).
    """
    point_index = cmd.point_index
    value = cmd.analog_value

    log.info(
        "Comando Analog #%d: ponto=%d tipo=%s valor=%.4f",
        cmd.command_id,
        point_index,
        pb.CommandType.Name(cmd.command_type),
        value,
    )

    # Escrever holding register no endereço Modbus correspondente
    try:
        modbus_client.write_register(point_index, value)
        responder(stub, cmd.command_id, pb.COMMAND_RESULT_SUCCESS)
    except ModbusTimeoutError:
        responder(stub, cmd.command_id, pb.COMMAND_RESULT_TIMEOUT)
    except Exception:
        responder(stub, cmd.command_id, pb.COMMAND_RESULT_DOWNSTREAM_FAIL)


def responder(stub, command_id, status):
    """Envia resposta de um comando ao bridge."""
    try:
        resp = stub.RespondToCommand(
            pb.CommandResponse(command_id=command_id, status=status)
        )
        if not resp.success:
            log.warning("Resposta ao comando #%d rejeitada (provavelmente timeout)", command_id)
    except grpc.RpcError as e:
        log.error("Falha ao responder comando #%d: %s", command_id, e.code())
```

### Como o bridge processa a resposta

Quando o Python chama `RespondToCommand`, o bridge:

1. Converte o `CommandResultStatus` do proto para o `CommandStatus` do opendnp3
2. Localiza o `std::promise` correspondente ao `command_id` na fila de pendentes
3. Define o valor da promise, o que desbloqueia o `future.wait_for()` no dispatch
4. O dispatch retorna o `CommandStatus` ao opendnp3, que responde ao SCADA

### Comportamento do SELECT/OPERATE no DNP3

O protocolo DNP3 usa um padrão de dois passos para comandos:

1. **SELECT**: O SCADA envia um pedido para "reservar" o ponto
2. **OPERATE**: O SCADA confirma e executa o comando

O bridge retorna **SUCCESS automaticamente** para todos os SELECT — a validação
real acontece apenas no OPERATE, quando o comando é encaminhado ao Python.
O Python nunca recebe SELECTs, apenas OPERATEs.

---

## 6. Fluxo 3: Consultar status da outstation

```python
def verificar_status(stub):
    """Consulta o estado da outstation DNP3."""
    try:
        resp = stub.GetStatus(pb.StatusRequest())

        # resp.state: OUTSTATION_STATE_UNKNOWN / CONNECTED / DISCONNECTED
        # resp.last_update_timestamp_ms: timestamp (ms) da última chamada UpdatePoints

        estado = pb.OutstationState.Name(resp.state)
        log.info("Outstation: %s (última atualização: %d ms)", estado, resp.last_update_timestamp_ms)

        return resp.state == pb.OUTSTATION_STATE_CONNECTED

    except grpc.RpcError as e:
        log.error("Falha ao consultar status: %s", e.code())
        return False
```

Use `GetStatus` para:
- Verificar se o bridge está rodando (conexão gRPC funciona)
- Verificar se a outstation está conectada ao SCADA (campo `state`)
- Monitorar se o loop de atualização está ativo (campo `last_update_timestamp_ms`)

---

## 7. Mapa de pontos DNP3

### Binary Input (11 pontos) — status do equipamento

Python lê do DSP via Modbus e envia com `UpdatePoints`.

| Índice | Nome | Modbus | Classe DNP3 |
|--------|------|--------|-------------|
| 0 | Equipamento Energizado | Discrete Input 0 | Classe 1 |
| 1 | Equipamento Operando | Discrete Input 1 | Classe 1 |
| 2 | Alarme | Discrete Input 2 | Classe 1 |
| 3 | Operação Local/Remoto | Discrete Input 3 | Classe 1 |
| 4 | Botoeira de Emergência | Discrete Input 4 | Classe 1 |
| 5 | Status Desequilíbrio | Discrete Input 5 | Classe 1 |
| 6 | Status Reativo | Discrete Input 6 | Classe 1 |
| 7 | Status Suporte de Tensão | Discrete Input 7 | Classe 1 |
| 8 | Status Regulação de Tensão | Discrete Input 8 | Classe 1 |
| 9 | Status Compensação Harmônica | Discrete Input 9 | Classe 1 |
| 10 | Erro de IGBT | Discrete Input 10 | Classe 1 |

### Analog Input (21 pontos) — medições elétricas

Python lê do DSP via Modbus e envia com `UpdatePoints`.

| Índice | Nome | Modbus | Classe DNP3 |
|--------|------|--------|-------------|
| 0 | Tensão Fase A | Input Register 0 | Classe 0 |
| 1 | Tensão Fase B | Input Register 1 | Classe 0 |
| 2 | Tensão Fase C | Input Register 2 | Classe 0 |
| 3 | Corrente Fase A | Input Register 3 | Classe 0 |
| 4 | Corrente Fase B | Input Register 4 | Classe 0 |
| 5 | Corrente Fase C | Input Register 5 | Classe 0 |
| 6 | Corrente Neutro | Input Register 6 | Classe 0 |
| 7 | THD Tensão Fase A | Input Register 7 | Classe 0 |
| 8 | THD Tensão Fase B | Input Register 8 | Classe 0 |
| 9 | THD Tensão Fase C | Input Register 9 | Classe 0 |
| 10 | THD Corrente Fase A | Input Register 10 | Classe 0 |
| 11 | THD Corrente Fase B | Input Register 11 | Classe 0 |
| 12 | THD Corrente Fase C | Input Register 12 | Classe 0 |
| 13 | Desequilíbrio Negativo | Input Register 13 | Classe 0 |
| 14 | Desequilíbrio Zero | Input Register 14 | Classe 0 |
| 15 | Tensão Link CC | Input Register 15 | Classe 0 |
| 16 | Temperatura Ponte | Input Register 16 | Classe 0 |
| 17 | Estado Atual de Operação | Input Register 17 | Classe 1 |
| 18 | Modo Reativo | Input Register 18 | Classe 1 |
| 19 | Modo Harmônicos | Input Register 19 | Classe 1 |
| 20 | Código de Falta | Input Register 20 | Classe 1 |

### Binary Output (20 pontos) — comandos do SCADA

Recebidos via `StreamCommands` como `COMMAND_TYPE_CROB`. Python executa no DSP via Modbus.

| Índice | Nome | Modbus | Classe DNP3 |
|--------|------|--------|-------------|
| 0 | Conectar Equipamento | Coil 0 | Classe 1 |
| 1 | Desconectar Equipamento | Coil 1 | Classe 1 |
| 2 | Reset Proteção | Coil 2 | Classe 1 |
| 3 | Emergência | Coil 3 | Classe 1 |
| 4 | Ativa Desequilíbrio | Coil 4 | Classe 1 |
| 5 | Desativa Desequilíbrio | Coil 5 | Classe 1 |
| 6 | Ativa Suporte de Tensão | Coil 6 | Classe 1 |
| 7 | Desativa Suporte de Tensão | Coil 7 | Classe 1 |
| 8 | Ativa Regulação de Tensão | Coil 8 | Classe 1 |
| 9 | Desativa Regulação de Tensão | Coil 9 | Classe 1 |
| 10 | Ativa Compensação Harmônica | Coil 10 | Classe 1 |
| 11 | Desativa Compensação Harmônica | Coil 11 | Classe 1 |
| 12 | Compensa Harmônica 3 | Coil 12 | Classe 0 |
| 13 | Compensa Harmônica 5 | Coil 13 | Classe 0 |
| 14 | Compensa Harmônica 7 | Coil 14 | Classe 0 |
| 15 | Compensa Harmônica 9 | Coil 15 | Classe 0 |
| 16 | Compensa Harmônica 11 | Coil 16 | Classe 0 |
| 17 | Compensação Harmônica Completa | Coil 17 | Classe 0 |
| 18 | Aciona Ventilador Painel | Coil 18 | Classe 0 |
| 19 | Aciona Ventilador Ponte | Coil 19 | Classe 0 |

### Analog Output (5 pontos) — setpoints do SCADA

Recebidos via `StreamCommands` como `COMMAND_TYPE_ANALOG_FLOAT32`. Python executa no DSP via Modbus.

| Índice | Nome | Modbus | Classe DNP3 |
|--------|------|--------|-------------|
| 0 | Ref. Regulação de Tensão | Holding Register 0 | Classe 2 |
| 1 | Limite Corrente Deseq. Neg. | Holding Register 1 | Classe 2 |
| 2 | Limite Corrente Deseq. Zero | Holding Register 2 | Classe 2 |
| 3 | Limite Corrente Reativo | Holding Register 3 | Classe 2 |
| 4 | Limite Corrente Harmônico | Holding Register 4 | Classe 2 |

### Classes DNP3 e polling

- **Classe 0**: Dados estáticos. Lidos quando o SCADA faz integrity poll.
- **Classe 1**: Eventos de alta prioridade. Reportados automaticamente quando mudam.
- **Classe 2**: Eventos de baixa prioridade.

O SCADA tipicamente faz:
- Integrity poll ao conectar (lê todos os pontos)
- Polls periódicos de Classe 1 e 2 para detectar mudanças
- Pode solicitar polls específicos a qualquer momento

---

## 8. Tratamento de erros e reconexão

### Cenários de erro

| Cenário | Comportamento | Ação do Python |
|---------|---------------|----------------|
| Bridge não está rodando | `grpc.RpcError` com `UNAVAILABLE` | Retry com backoff |
| Bridge reiniciou | Stream `StreamCommands` desconecta | Reconectar automaticamente |
| Timeout de comando | `RespondToCommand` retorna `success=false` | Logar, não reenviar |
| Rede entre Python e bridge cai | Todas as RPCs falham | Reconectar canal gRPC |

### Padrão de reconexão para o StreamCommands

```python
def command_listener(stub):
    backoff = 2  # segundos
    while not shutdown:
        try:
            stream = stub.StreamCommands(pb.StreamCommandsRequest())
            for cmd in stream:
                processar_comando(stub, cmd)
        except grpc.RpcError:
            if not shutdown:
                log.warning("Stream desconectou. Reconectando em %ds...", backoff)
                time.sleep(backoff)
```

### SIGPIPE

O bridge ignora `SIGPIPE` — se o Python desconectar durante um `StreamCommands`,
o bridge não crasha. Ele detecta a desconexão e retorna `DOWNSTREAM_FAIL` para
qualquer comando SCADA que chegar enquanto não houver stream ativo.

---

## 9. Threading e concorrência

### Modelo recomendado

```
Thread principal
  ├── Loop de leitura Modbus (periódico, ex: a cada 1s)
  │     └── stub.UpdatePoints(...)
  │
  ├── Thread de comando (daemon)
  │     └── stub.StreamCommands(...) → processar → stub.RespondToCommand(...)
  │
  └── Monitoramento (opcional)
        └── stub.GetStatus(...)
```

### Pontos importantes

- O `stub` do gRPC é **thread-safe** — pode ser compartilhado entre threads
- `UpdatePoints` e `RespondToCommand` são chamadas unárias rápidas (< 10ms)
- `StreamCommands` é bloqueante — **deve** rodar em thread separada
- A thread de comandos deve ser daemon para não impedir shutdown
- Use um `threading.Event` para coordenar shutdown limpo

### Ordem de inicialização

1. Criar canal gRPC e stub
2. Chamar `GetStatus` para verificar que o bridge está ativo
3. Iniciar thread de `StreamCommands` (listener de comandos)
4. Iniciar loop principal de leitura Modbus + `UpdatePoints`

### Ordem de shutdown

1. Sinalizar evento de shutdown
2. O loop principal para de ler Modbus
3. A thread de comandos detecta shutdown e sai do loop
4. Fechar o canal gRPC (`channel.close()`)

---

## 10. Exemplos completos em Python

### Exemplo mínimo funcional

```python
#!/usr/bin/env python3
"""Exemplo mínimo de integração com o dnp3-bridge."""

import signal
import threading
import time

import grpc

# Importar os stubs gerados (ver seção 2)
from generated import dnp3bridge_pb2 as pb
from generated import dnp3bridge_pb2_grpc as pb_grpc

shutdown = threading.Event()


def command_listener(stub):
    """Escuta comandos SCADA em loop com reconexão automática."""
    while not shutdown.is_set():
        try:
            stream = stub.StreamCommands(pb.StreamCommandsRequest())
            for cmd in stream:
                if shutdown.is_set():
                    break

                # Processar comando (adaptar para sua lógica Modbus)
                print(f"Comando #{cmd.command_id}: tipo={cmd.command_type} ponto={cmd.point_index}")

                # Responder SUCCESS (substituir pela lógica real)
                stub.RespondToCommand(
                    pb.CommandResponse(
                        command_id=cmd.command_id,
                        status=pb.COMMAND_RESULT_SUCCESS,
                    )
                )

        except grpc.RpcError:
            if not shutdown.is_set():
                time.sleep(2)


def main():
    signal.signal(signal.SIGINT, lambda *_: shutdown.set())
    signal.signal(signal.SIGTERM, lambda *_: shutdown.set())

    channel = grpc.insecure_channel("localhost:50051")
    stub = pb_grpc.BridgeServiceStub(channel)

    # Verificar conexão
    try:
        status = stub.GetStatus(pb.StatusRequest())
        print(f"Bridge conectado. Estado: {pb.OutstationState.Name(status.state)}")
    except grpc.RpcError:
        print("Bridge não disponível!")
        return

    # Iniciar listener de comandos
    cmd_thread = threading.Thread(target=command_listener, args=(stub,), daemon=True)
    cmd_thread.start()

    # Loop principal de atualização de pontos
    while not shutdown.is_set():
        request = pb.UpdateRequest(
            analogs=[
                pb.AnalogPoint(index=0, value=220.0, quality=pb.POINT_QUALITY_GOOD),
            ],
            binaries=[
                pb.BinaryPoint(index=0, value=True, quality=pb.POINT_QUALITY_GOOD),
            ],
        )
        try:
            stub.UpdatePoints(request)
        except grpc.RpcError:
            pass

        shutdown.wait(timeout=1.0)

    channel.close()
    print("Encerrado.")


if __name__ == "__main__":
    main()
```

### Referência completa

Para uma implementação mais robusta com todos os pontos, tratamento de erros e interface
interativa, veja o simulador em `tools/python-dsp-sim/dsp_sim.py` (~430 linhas) e
a versão TUI em `tools/python-dsp-sim/tui.py` (~500 linhas).

---

## 11. Checklist de integração

- [ ] Gerar stubs Python a partir do `.proto` (`generate_proto.sh`)
- [ ] Criar canal gRPC para `localhost:50051`
- [ ] Implementar `GetStatus` para health check na inicialização
- [ ] Implementar loop de `UpdatePoints` com todos os 11 BI + 21 AI
- [ ] Implementar thread de `StreamCommands` com reconexão automática
- [ ] Implementar processamento de `COMMAND_TYPE_CROB` (20 pontos)
- [ ] Implementar processamento de `COMMAND_TYPE_ANALOG_FLOAT32` (5 pontos)
- [ ] Implementar `RespondToCommand` com status correto para cada caso
- [ ] Usar `POINT_QUALITY_BAD` quando Modbus falhar
- [ ] Testar timeout: desabilitar auto-respond no simulador e verificar que SCADA recebe TIMEOUT
- [ ] Testar reconexão: reiniciar bridge com Python rodando
- [ ] Testar com o simulador C++ Master (TUI) para validar polling e comandos
