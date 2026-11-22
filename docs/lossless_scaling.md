# Modo de compatibilidade com o Lossless Scaling

Desde a versão 0.7.9.4, o ProjectReLinkAMD tem um modo que prepara a imagem para o [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) (LS) rodando por cima do jogo. Esse modo não injeta nada no LS e não mexe nele. Ele só muda a forma como o próprio ReLink apresenta os quadros, para que a captura do LS receba imagens limpas e em ritmo previsível.

O código fica em `gpu/vk/ls_compat.h`, e a tabela de perfis é verificada por `gpu/vk/tests/ls_compat_test.cpp`.

## Como ativar

Você pode escolher o perfil de qualquer uma destas formas. Quando mais de uma está definida, a primeira da lista vale:

1. A variável `RX_VK_LOSSLESS_SCALING` no `relink_env.txt`, que aceita `0` a `5` ou os nomes `off`, `scaling`, `lsfg`, `lsfg_scaling`, `performance` e `compat`. Essa variável **nunca** é gravada nas suas opções: se você apagar a linha, volta a valer o que está no menu.
2. O menu Insert, em Graphics → Smoothness → **Lossless Scaling**.
3. O launcher, em Smoothness → **Lossless Scaling**, que grava a chave `lossless_scaling` no `relink_options.ini`.

Para voltar ao comportamento original, escolha **Off**. O perfil nunca reescreve as outras opções. O Frame generation, o filtro de janela, o Sharpness e o V-Sync continuam com os valores que você escolheu, e voltam a funcionar assim que o perfil é desligado.

## Perfis

| | Off | LS Scaling | LSFG | LSFG + Scaling | LS Performance | LS Max Compatibility |
|---|---|---|---|---|---|---|
| Frame generation do ReLink | menu | menu | **desligado** | **desligado** | **desligado** | **desligado** |
| Filtro de janela e Sharpness | menu | **desligados** | menu | **desligados** | **desligados** | **desligados** |
| Repetições do "30, smooth" | sim | sim | **não** | **não** | **não** | **não** |
| Tela cheia exclusiva | driver decide | **proibida** | **proibida** | **proibida** | **proibida** | **proibida** |
| Modo de apresentação | V-Sync | V-Sync | V-Sync | V-Sync | **immediate** | **FIFO** |
| Imagens da swapchain | 4 | 4 | 4 | 4 | **3** | **3** |
| Fast sync | padrão | padrão | padrão | padrão | padrão | **desligado** (após reiniciar) |

O que cada mudança faz e por quê:

- **Frame generation do ReLink desligado.** Com ele ligado, o LSFG geraria quadros entre quadros que já foram gerados. O resultado seriam artefatos acumulados e latência somada.
- **Filtro de janela e Sharpness desligados.** O LS já faz a escala. Uma escala do ReLink seguida da escala do LS piora a nitidez e gasta GPU sem ganho. Para ter uma única escala, a janela precisa ter o tamanho da imagem: o log avisa quando não tem (`ls: the picture is ... and the window ...`).
- **Sem repetições no "30, smooth".** Esse limite mostra o mesmo quadro várias vezes. Para o LSFG, isso parece um jogo travado. Nos perfis LS, o limite abaixo de 60 fps passa a ser feito só por timer, cada quadro é apresentado uma vez, e o limite deixa de forçar FIFO.
- **Tela cheia exclusiva proibida** (`VK_EXT_full_screen_exclusive`, `DISALLOWED`). O Fullscreen do ReLink é uma janela sem bordas, mas sem essa extensão o driver pode apresentá-la de forma exclusiva. Nesse caso, a captura DXGI e o overlay do LS deixam de funcionar. A extensão só é ligada quando o jogo **inicia** com um perfil LS, então trocar no menu pede um reinício para esta parte. No perfil Off nada novo é habilitado.
- **Immediate** (LS Performance). Os presents saem sem esperar o refresh, o que dá a menor latência. O LS cuida da própria sincronização.
- **FIFO e fast sync desligado** (LS Max Compatibility). Cada present cai num refresh, então a captura vê um ritmo regular. O fast sync só foi validado em placas NVIDIA, e esse perfil volta para a espera antiga.
- **3 imagens na swapchain.** Fica um quadro a menos na fila do que com 4, o que reduz a latência.

### Controles individuais

Cada parte pode ser ajustada sozinha no `relink_env.txt`. Esses ajustes só valem com um perfil ligado:

| Variável | Valores |
|---|---|
| `RX_VK_LS_INTERNAL_FG` | `1` permite o frame generation do ReLink |
| `RX_VK_LS_WINDOW_FILTER` | `1` permite o filtro de janela e o Sharpness |
| `RX_VK_LS_REPEATS` | `1` permite as repetições do "30, smooth" |
| `RX_VK_LS_FSE` | `default`, `disallow` ou `allow` |
| `RX_VK_LS_PRESENT` | `auto` (segue o V-Sync), `fifo` ou `immediate` |
| `RX_VK_LS_IMAGES` | `0` (como sem o modo), `2`, `3` ou `4` |
| `RX_VK_LS_FAST_SYNC` | `0` ou `1` (vale depois de reiniciar; `RX_VK_FAST_SYNC` continua tendo prioridade) |

## Configuração recomendada (RX 9060 XT, monitor de 180 Hz, captura DXGI, tela cheia)

- Deixe o jogo no monitor de **180 Hz**. O ReLink lê o refresh do monitor onde a janela está.
- Use o perfil **LSFG**. O LS Scaling só faz sentido em janela menor que a tela, e você joga em tela cheia.
- Escolha o FPS base de acordo com o multiplicador do LSFG:
  - **3x com o jogo em 60 fps**, o padrão do jogo. É o caminho mais seguro e chega a 180 fps apresentados.
  - **2x** com `HOS_FPS_MAX=90` no `relink_env.txt`.
  - **4x** com `HOS_FPS_CAP=45`, que só fica bom com as repetições desligadas pelo perfil.
- Deixe o V-Sync do ReLink desligado e use a sincronização do LS.
- Deixe o contador de FPS do ReLink desligado e use o do LS. Texto pequeno desenhado sobre a imagem costuma gerar artefatos no LSFG.

## Como medir

- **PresentMon, CapFrameX ou OCAT.** Os presents do `relink-game.exe` são o FPS **real**, e os do `LosslessScaling.exe` são o FPS **apresentado**. Não misture os dois números.
- **Log do ReLink.** `RX_VK_FRAME_LOG=ft.csv` registra o frame time de cada quadro do jogo. A linha `VRAM x of y GB` aparece no log a cada 600 quadros.
- **Teste por cenário.** Rode cada cenário (Off sem LS, LS Scaling, LSFG, LSFG + Scaling, LS Max Compatibility) nos mesmos trechos, por exemplo o Campo de Hyrule correndo por 60 s e Kakariko parado por 30 s. Repita 3 vezes e faça uma sessão longa de 30 a 60 minutos.
- **Profiling separado.** `RX_VK_PASS_TIMING`, `RX_VK_GPU_PROFILE` e `RX_VK_TIMELINE` mudam o comportamento do jogo, então rode-os em sessões separadas das medições finais.
- **Monitor de 60 Hz.** Faça uma rodada com o monitor de 60 Hz desligado. Monitores com refresh diferentes podem deixar a composição do Windows irregular.

## Limites

- Não há garantia de compatibilidade universal. O resultado depende da versão do LS, do driver Adrenalin e da API de captura.
- A tela cheia exclusiva implícita no driver AMD é uma hipótese. Se o overlay do LS funcionar também com o perfil Off, a proibição vira só uma proteção extra.
