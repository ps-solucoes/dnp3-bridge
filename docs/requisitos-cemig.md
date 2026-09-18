# Requisitos CEMIG — Conversor DNP3

Documento de requisitos consolidado a partir do Q&A entre equipe de desenvolvimento e stakeholder CEMIG.

> Referência: Mapas de comunicação em `docs/Mapa_comunicacao.md` e `docs/Mapa_comunicacao_Scada_v1.md`

---

## Resumo de Status

| Status | Quantidade |
|--------|-----------|
| Implementado | 12 |
| **Total** | **12** |

---

## REQ-01 — Unsolicited Response para Binary Input

**Status**: IMPLEMENTADO

**Descrição**: O conversor deve gerar Unsolicited Response (UR) a cada variação de Binary Input.

**Fonte (stakeholder)**:
> "Perfeito, esta é uma regra que precisamos na CEMIG."
> "Precisamos de UR somente para Binary Input, Binary Output não deve gerar UR."

**Implementação**: `allowUnsolicited = true` e `unsolClassMask` configurado para Classes 1 e 2 via `buildClassMask(cfg_.unsolicited.class_mask)`. Binary Input é Classe 1, portanto variações geram URs. A configuração é dinâmica via JSON (`unsolicited.class_mask`).

**Arquivo**: `src/dnp3/OutstationManager.cpp`

---

## REQ-02 — Binary Output NÃO deve gerar UR

**Status**: IMPLEMENTADO

**Descrição**: Binary Output Status não deve gerar Unsolicited Response.

**Fonte (stakeholder)**:
> "Precisamos de UR somente para Binary Input, Binary Output não deve gerar UR."

**Implementação**: Todos os Binary Output Status (índices 0-19) configurados como Classe 0 no database default. Classe 0 não gera eventos nem URs. Configurável via JSON (`point_database.binary_output_status[].class`).

**Arquivo**: `src/dnp3/OutstationManager.cpp`

---

## REQ-03 — Fila de eventos mínimo 100

**Status**: IMPLEMENTADO

**Descrição**: A fila de eventos DNP3 deve ser armazenada em memória volátil com capacidade mínima de 100 eventos.

**Fonte (stakeholder)**:
> "No mercado hoje temos na maioria dos equipamentos a fila de eventos em memória volátil mesmo e com no mínimo 100 eventos."

**Implementação**: `EventBufferConfig` default com total de 100 eventos (50 BI + 50 AI). Configurável via JSON (`event_buffer.max_binary_events`, `event_buffer.max_analog_events`, etc.).

**Arquivo**: `src/config/AppConfig.hpp`, `src/dnp3/OutstationManager.cpp`

---

## REQ-04 — Confirmação de aplicação para URs com retransmissão infinita

**Status**: IMPLEMENTADO

**Descrição**: URs devem ter confirmação na camada de aplicação. Em caso de falha de comunicação, o IED deve reenviar as mensagens. Nunca desabilitar URs.

**Fonte (stakeholder)**:
> "No caso do SCADA da CEMIG será utilizado confirmação na camada de aplicação e somente não teremos confirmação se tivermos um problema de falha de comunicação e neste caso o SCADA irá reiniciar a comunicação e neste caso o IED deve reenviar as mensagens. Não podemos desligar as URs."
> "URs terão confirmação na camada de aplicação e Classe 1/2/3 se tiver evento devolve o evento e caso não informa sem evento."

**Implementação**: opendnp3 default `numUnsolRetries = NumRetries::Infinite()` (`OutstationParams.h:50`). Retransmissão infinita habilitada. Leituras de Classe 1/2/3 retornam eventos quando existem (comportamento padrão do protocolo DNP3).

---

## REQ-05 — Tratamento de buffer overflow

**Status**: IMPLEMENTADO

**Descrição**: Tratar situações de Buffer Overflow na fila de eventos.

**Fonte (stakeholder)**:
> "Temos que ficar atentos nestes casos com Buffer Over Flow e tratar este item."

**Implementação**: opendnp3 descarta automaticamente os eventos mais antigos quando o buffer está cheio. Este é o comportamento padrão da biblioteca.

---

## REQ-06 — Modo de comando configurável (Direct Operate vs SBO)

**Status**: IMPLEMENTADO

**Descrição**: O modo de operação de comandos (Direct Operate ou Select-Before-Operate) deve ser configurável. CEMIG utilizará Direct Operate com objeto CROB (10/12).

**Fonte (stakeholder)**:
> "Neste caso o ideal seria configurável, e a CEMIG irá usar comandos Direct e não SBO. E o Objeto será o 10/12 OK."

**Implementação**: Campo `command_mode` na configuração (`"direct_operate"` | `"select_before_operate"`). Default: `"direct_operate"`. Enum `CommandMode` em `ForwardingCommandHandler`. No modo Direct Operate, todos os 5 métodos `Select()` retornam `NOT_SUPPORTED`. No modo SBO, retornam `SUCCESS`. Configurável via JSON e variável de ambiente `DNP3_BRIDGE_COMMAND_MODE`.

**Arquivos**: `src/config/AppConfig.hpp`, `src/config/ConfigLoader.cpp`, `src/dnp3/ForwardingCommandHandler.hpp`, `src/dnp3/ForwardingCommandHandler.cpp`, `src/dnp3/OutstationManager.cpp`

---

## REQ-07 — Classes dos pontos configuráveis

**Status**: IMPLEMENTADO

**Descrição**: As classes dos pontos (Classe 0/1/2/3) devem ser configuráveis. Analog Input default em Classe 2.

**Fonte (stakeholder)**:
> "As variáveis de entradas analógicas serem default em classe 2 e com configuração de banda morta. O ideal seria as classes dos pontos serem configuráveis."

**Implementação**: Seção `point_database` no JSON config permite definir classe por ponto ou faixa. Suporta sintaxe `"index": N` (ponto único) e `"range": [start, end]` (faixa inclusiva). Defaults preservam: BI=Classe 1, BO=Classe 0, AI=Classe 2, AO=Classe 2.

**Arquivos**: `src/config/AppConfig.hpp`, `src/config/ConfigLoader.cpp`, `src/dnp3/OutstationManager.cpp`

---

## REQ-08 — Banda morta configurável para Analog Input

**Status**: IMPLEMENTADO

**Descrição**: As entradas analógicas devem ter banda morta (deadband) configurável. Eventos analógicos só são gerados quando a variação excede o valor da banda morta.

**Fonte (stakeholder)**:
> "As variáveis de entradas analógicas serem default em classe 2 e com configuração de banda morta."

**Implementação**: Campo `deadband` na configuração de pontos analógicos (default: 0.0). Aplicado via `db_config.analog_input[i].deadband` e `db_config.analog_output_status[i].deadband`. Configurável por ponto ou faixa no JSON.

**Arquivos**: `src/config/AppConfig.hpp`, `src/config/ConfigLoader.cpp`, `src/dnp3/OutstationManager.cpp`

---

## REQ-09 — Sincronismo de relógio via Objeto 50

**Status**: IMPLEMENTADO

**Descrição**: O SCADA fará sincronismo de relógio do conversor com processo Delay Measurement + Write Time via Objeto 50 do DNP3, normalmente a cada 3600 segundos.

**Fonte (stakeholder)**:
> "Sim teremos acerto de data e hora via Objeto 50 do DNP normalmente a cada 3600 Segundos, podendo ser diferente dependendo da mídia de comunicação."

**Implementação**: `DefaultOutstationApplication` implementa `SupportsWriteAbsoluteTime() = true` e `WriteAbsoluteTime()` (`DefaultOutstationApplication.h:50-54`). O refresh rate default é 1 minuto, configurável via parâmetro do construtor.

---

## REQ-10 — Varredura de classes pelo SCADA

**Status**: IMPLEMENTADO

**Descrição**: O SCADA realizará Read Class 123 periodicamente (ex: cada 3 min) e Read Class 0 para integridade (ex: cada 60 min). Parâmetros dependem da mídia de comunicação.

**Fonte (stakeholder)**:
> "Normalmente para um canal celular que é o mais comum seria Read Class 123 a cada 3 minutos + Read Class 0 a cada 60 minutos."

**Implementação**: Inerente ao protocolo outstation DNP3. A opendnp3 responde automaticamente a requisições de leitura de classes.

---

## REQ-11 — Transição Quality Online/Offline NÃO gera UR

**Status**: IMPLEMENTADO

**Descrição**: A transição de Quality Online para Offline (e vice-versa) não deve gerar Unsolicited Response.

**Fonte (stakeholder)**:
> "A princípio não."

**Implementação**: NÃO é o comportamento padrão da opendnp3 — `measurements::IsEvent()`
(`EventTriggers.cpp:28`) retorna `true` para qualquer diferença de flags, antes de avaliar a
banda morta. Para binários (`MeasurementTypeSpecs.h:43`) a comparação é exclusivamente de flags.

Por isso a decisão de gerar evento é tomada em `OutstationManager::updateAnalog()` /
`updateBinary()`, e não delegada ao `EventMode::Detect`:

- somente o **valor** decide (analógico: variação > banda morta; binário: mudança de estado);
- o update é aplicado com `EventMode::Force` quando há evento e `EventMode::Suppress` quando não há;
- `Suppress` atualiza o valor estático e as flags sem gerar evento, portanto a qualidade chega
  ao SCADA nas leituras de Classe 0 sem gerar UR.

A referência da banda morta é o último valor **com evento**, não o último valor aplicado.

**Arquivos**: `src/dnp3/OutstationManager.cpp`, `src/bridge/DataModel.hpp`

---

## REQ-12 — Variáveis AI em 16-bit inteiro

**Status**: IMPLEMENTADO

**Descrição**: Todas as variáveis analógicas devem ser 16-bit inteiro (sem ponto flutuante).

**Fonte (stakeholder)**:
> "No caso do SCADA CEMIG na tabela de pontos proposta não vejo necessidade de nenhuma variável de 32 Bits e sim somente 16 bits com números inteiros, ou seja, sem ponto flutuante."

**Implementação**: Analog Input configurado com `Group30Var2` (static, 16-bit signed integer) e `Group32Var2` (event, 16-bit signed integer with timestamp). Analog Output Status com `Group40Var2` / `Group42Var2`. Variações são configuráveis via JSON (`static_variation`, `event_variation`).

**Arquivo**: `src/dnp3/OutstationManager.cpp`

---

## Observações adicionais do stakeholder

### Leitura por tipo de objeto

> "Teremos casos que as leituras serão feitas por tipo de Objeto Ex 2,22,32 de varredura ou 1,20,30 de integridade [...] no caso da variação de resposta entendo que a mesma será configurada no IED no momento da criação da integração."

A opendnp3 responde automaticamente com a variação configurada na `DatabaseConfig` para cada tipo de ponto. Atendido pela configuração de `svariation`/`evariation`.

### Confirmação na camada de enlace

O stakeholder não respondeu especificamente sobre confirmação de enlace. A opendnp3 utiliza por padrão sem confirmação de enlace. Se necessário, pode ser habilitado via `LinkConfig.ConfirmTimeout`.

---

## Rastreabilidade de arquivos

| Arquivo | Requisitos relacionados |
|---------|------------------------|
| `src/dnp3/OutstationManager.cpp` | REQ-01, 02, 03, 07, 08, 09, 12 |
| `src/config/AppConfig.hpp` | REQ-03, 06, 07, 08 |
| `src/config/ConfigLoader.cpp` | REQ-06, 07, 08 |
| `src/dnp3/ForwardingCommandHandler.hpp` | REQ-06 |
| `src/dnp3/ForwardingCommandHandler.cpp` | REQ-06 |
| `config.example.json` | REQ-01, 02, 03, 06, 07, 08, 12 |

---

## REQ-13 — Qualidade dos pontos reportada ao SCADA

**Status**: IMPLEMENTADO

**Descrição**: A qualidade informada pelo lado Python deve ser refletida nas flags DNP3 dos pontos,
visível nas leituras estáticas (Classe 0), sem gerar URs (ver REQ-11).

**Fonte (stakeholder)**: Confirmado após a constatação de que o campo `quality` do gRPC era descartado.

**Implementação**: `PointQuality` (gRPC) → `bridge::Quality` → flags DNP3 em `toFlagBits()`:

| `PointQuality` | Flags DNP3 |
|---|---|
| `GOOD` | `ONLINE` (0x01) |
| `UNCERTAIN` | `ONLINE` (0x01) |
| `BAD` | `0x00` (ONLINE removido = offline) |
| `RESTART` | `ONLINE \| RESTART` (0x03) |

**Nota**: `RESTART` mantém o bit `ONLINE` ligado. O Python envia um valor junto com a flag,
portanto o valor é válido e apenas antecede o restart; `RESTART` isolado (0x02) é o marcador da
opendnp3 para um ponto que nunca recebeu valor algum.

**Nota**: `UNCERTAIN` é mapeado para `ONLINE`, ficando indistinguível de `GOOD` no protocolo. O DNP3
não possui equivalente direto; as alternativas eram `LOCAL_FORCED` e `REFERENCE_ERR`. Decisão da
CEMIG. Reavaliar com o stakeholder caso dados incertos precisem ser visíveis no SCADA.

**Arquivos**: `src/bridge/DataModel.hpp`, `src/grpc/BridgeServiceImpl.cpp`, `src/dnp3/OutstationManager.cpp`
