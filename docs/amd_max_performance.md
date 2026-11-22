# AMD MAX PERFORMANCE

O subsistema do ProjectReLinkAMD que entende o que a placa Radeon e o driver dela conseguem fazer e escolhe o caminho
de execução a partir disso. Vale para toda a linha Radeon com Vulkan 1.3 (RX 6000, RX 7000, RX 9000 e as integradas),
não só para a RX 9060 XT, que é uma das placas de referência.

Regra do projeto: nada vira padrão sem número de uma partida real. Este documento separa o que foi medido em placa
física do que só foi verificado por testes sem GPU.

## Estado por etapa

| Etapa | Versão | O que faz | Muda o jogo? | Validado em placa física |
| --- | --- | --- | --- | --- |
| 1. Detecção e log | 0.7.9.8 | Matriz de capacidades e tabela de problemas conhecidos, no log | Não | Ainda não |
| 2. Perfis e benchmark | planejada | `RX_VK_AMD_PROFILE`, CSV de benchmark | Só com o perfil escolhido | - |
| 3. Matrizes WMMA | planejada | Rede TAAU em WMMA 16x16x16 (RDNA 3/4), opt-in | Só com `RX_VK_MMA_WMMA=1` | - |
| 4. Task/mesh e núcleos | planejada | Otimizador em task/mesh, workers nos núcleos P | Só se o benchmark provar | - |
| 5. Texturas | planejada | BC1/BC4/BC5 por conteúdo para o ASTC decodificado | Só se a comparação de imagem aprovar | - |

## Arquitetura

- `gpu/vk/amd_caps.h`: a matriz de capacidades (`amdmax::Caps`) e a geração. Puro, sem Vulkan.
- `gpu/vk/amd_quirks.h`: a tabela de problemas conhecidos, com onde cada um foi visto e o que o desliga.
- `gpu/vk/vk_main.cpp` (`amd_max_report`): preenche a matriz depois de criar o device e escreve as linhas `vk: AMD MAX:`.
- `gpu/vk/tests/amd_caps_test.cpp`: testes sem GPU (RX 9060 XT, RX 6600, RX 7900 com driver antigo, placa AMD
  desconhecida, GCN, Radeon 890M no driver da AMD e no RADV, NVIDIA e Intel).

Próximas etapas: `gpu/vk/amd_policy.h` (perfil + capacidades + problemas conhecidos -> decisões) e
`gpu/vk/tex_format_policy.h` (formatos de textura).

## Como a placa é identificada

As decisões vêm sempre das capacidades que o driver informa: tamanhos de subgrupo e em que estágios dá para fixar 32
lanes, mesh shaders, formatos de matriz cooperativa, f16, atomics float, BC e ASTC, heaps de VRAM, memory budget,
Anti-Lag 2. A geração (RDNA 2, 3, 3.5, 4) serve só para o log e para a tabela de problemas:

1. Pelo device ID, quando ele está na tabela de `gen_by_device_id`.
2. Senão, pelas capacidades: só 64 lanes = GCN; sem mesh = RDNA 1; mesh sem WMMA = RDNA 2; com WMMA = "RDNA 3 or later".
3. Quando o ID e as capacidades discordam (ex.: RX 7900 com driver que não oferece matriz cooperativa), o log diz
   "the capabilities say otherwise: an old driver?".

Uma placa que não está na tabela nunca perde recurso por isso.

## O que o log mostra (0.7.9.8)

Só em placas AMD, logo depois da linha `vk: AMD:`:

```
vk: AMD MAX: cooperative matrix shapes the driver offers: 16x16x16 A0 B0 C0 R0 scope 3, ...
vk: AMD MAX: AMD Radeon RX 9060 XT (1002:7590), RDNA 4 from the device ID; AMD proprietary <versão>, Vulkan 1.4; discrete
vk: AMD MAX: VRAM <MB>, RAM <MB> (<MB> free), memory budget reported
vk: AMD MAX: subgroups 64 lanes (32-64), held to 32 on compute; drawing stages translated for 64 lanes, mesh shaders yes, ...
vk: AMD MAX: cooperative matrices 16x16x16 f16; multiplies in software (the native path needs 16x8x16; WMMA's 16x16x16 not used yet)
vk: AMD MAX: CPU 16 threads, 6 performance and 4 efficiency cores
vk: AMD MAX: known problem mesh_no_optimizer: ...; seen on RX 6600 ... (applied here by caution: not seen on this generation); off: RX_VK_MESH_OPT=1
vk: AMD MAX: 4 known problem(s) applied; this version only reports, nothing chosen from it
```

(O exemplo mostra o formato; os valores reais saem da placa.)

## Problemas conhecidos

| Chave | O que o ReLink faz | Visto em | Desliga com |
| --- | --- | --- | --- |
| `mesh_no_optimizer` | Task/mesh sem o otimizador do driver | RX 6600 (issues 461, 484) | `RX_VK_MESH_OPT=1` |
| `compiler_fail_13` | Refaz sem otimizador a pipeline em que o compilador da AMD falha | RX 6600 | `RX_VK_NOOPT=1` faz todas assim |
| `wave64_drawing` | Vertex/fragment traduzidos para 64 lanes | RX 6600 | `RX_VK_NO_BIG_WARP=1` |
| `uneven_bc_levels` | ASTC de grade irregular fica RGBA8 | RX 6600 XT, RX 6700 XT | `RX_VK_UNEVEN_BC=1` |
| `fifo_at_startup` | Tela de início sem FIFO | RX 6600 XT | - |
| `igpu_wait_gpu` | iGPU: cada present espera o frame | Radeon 890M | `RX_VK_WAIT_GPU=-1` |
| `igpu_astc_cpu` | iGPU: ASTC na CPU | Radeon 890M, ROG Xbox Ally X | `RX_VK_ASTC_CPU=0` |

Hoje todos valem para toda Radeon (os de iGPU só nas integradas com o driver da AMD), porque só foram vistos em RDNA 2 e
3.5 e ninguém testou nas outras. O log marca "applied here by caution" quando a geração da placa não é onde o problema
foi visto. Tirar um deles de uma geração exige um teste nela.

## Variáveis

| Variável | Padrão | O que faz |
| --- | --- | --- |
| `RX_VK_AMD_MAX_LOG` | `1` | `0`: sem as linhas `vk: AMD MAX:` |

## Como adicionar uma GPU nova

1. Acrescente a faixa de device IDs do chip em `gen_by_device_id` (`amd_caps.h`) e, se for uma geração nova, um valor
   em `Gen` e em `gen_name`.
2. Acrescente um caso em `gpu/vk/tests/amd_caps_test.cpp` e rode:
   `g++ -std=c++17 -I gpu/vk gpu/vk/tests/amd_caps_test.cpp -o amd_caps_test && ./amd_caps_test`.
3. Para um problema novo, uma linha em `amd_quirks.h` com onde foi visto e o que o desliga.

## Limitações

- Não há placa Radeon nos testes automáticos. RX 6000 e RX 7000 só foram verificadas pelos testes sem GPU.
- O que o driver do Windows informa sobre subgrupos e matrizes cooperativas na RX 9060 XT só se sabe pelo log de uma
  partida real.
