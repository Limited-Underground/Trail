#!/usr/bin/env python3
"""Audit every known checkout-sensitive raw SHA-256 contract input."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "OTRBC0"


@dataclass(frozen=True)
class Entry:
    path: str
    length: int
    sha256: str
    policy: str
    eol: str
    bom: str
    final_newline: str


# Reviewed bytes already bound by raw SHA-256 validators. This registry is
# intentionally independent of the current checkout bytes.
REGISTRY = """
docs/decisions/0003-crypto-benchmark-gate.md|2033|6a88a00aba6383e07e1d1ca8daed3c48972806bc384e6d3980d27ea67accf359|lf|lf|none|lf
docs/decisions/0068-host-only-monocypher-start-ready-protocol-correction.md|2279|abc6212fde0cc4dd3df612c746ca8b9a1ce82869897beb911a883bb8e42c2448|lf|lf|none|lf
docs/decisions/0071-host-only-monocypher-opaque-preamble-correction.md|2434|ed86f244673d5ded814fb1c2a1d392b52a311977897378b854476c0ae319b23e|lf|lf|none|lf
docs/decisions/0074-host-only-monocypher-byte-bounded-preamble-correction.md|3040|cbefae9fb4e8c3b2179b8bbd486f4e2bb03ddd76eb2d3d2921a23f8792942cde|lf|lf|none|lf
docs/decisions/0077-classify-monocypher-boot-control-transport-conflict.md|3759|efc4d25d6e2abb455d804caa411d33cb543fb47399369ffedd11daf427ae0286|lf|lf|none|lf
tests/hardware/OT-138-2026-08-25.md|4054|f60cc203ea5ab1842b9519b2d95dd81594ef06eaf86e0add785c94bfb2903cd7|lf|lf|none|lf
tests/host/ot138_monocypher_boot_control_investigation_tests.py|5958|9ef68ead05ef6cbf9f2febb7af0b20ad5093bcb01f5c967f69e55db97f58bb88|lf|lf|none|lf
tools/ot138_monocypher_boot_control_investigation.py|14273|617ba545a5a3770ce4eaa10ff5b42c667757b94e82f6185e69228f5d42a2d8bf|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/include/ot121_benchmark_frame.h|7541|cc5a4596400a8a2766e66fcab6b7d51dceecbc4f3c1be4055a2581d400415d4c|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/libsodium/sdkconfig.overlay|35|b7b722dc1bcc2c5917bee365f2123171ec398b0a0f295d61e7a7e8c26b99c832|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/CMakeLists.txt|214|7fe5e93aff6f130b88cc0eed244cb1a21163693483cf2bf33dec184a7f9d08e3|lf|lf|none|none
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/CMakeLists.txt|738|ad8f3ad9d5b836a5a18e1f7059e0bbaf3adb238e058bd0b1e9021cd2f201621c|exact|mixed|none|none
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/app_main.c|22620|fac7a9375a5dba5366215dc0eab0a03a83cfd22fd50a2ac563f1c378cb7aae2b|exact|mixed|none|none
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.c|1752|d10b9e6769676530c4eacd7e31bbf2192f293f1fa1099138673525bc395bdcdf|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/main/ot129_control_protocol.h|830|14e2896e43e9a873ffb0fbfc4ec01c371095f39b42a6f82b2544ecf0f7c57e76|exact|mixed|none|crlf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher-result-frame.schema.json|8532|516cab7753d1ca22f59181480df4eed4aec232150035cee6a7a116fbb539c0e0|lf|lf|none|lf
tests/benchmarks/crypto/OT-005-CRYPTO-BENCHMARK-PLAN-V0.json|1925|47c210c6257cd104d07f8e043f2cd1c688195136bbd3fcbafb8e6da095d18884|lf|lf|none|lf
tests/benchmarks/crypto/OT-093-OT005-BUILD-BASELINE-V0.json|7585|240906d62926048e6f55b1bb11ce21538e24edbeb8956439ffeb35f3b49b3c83|lf|lf|none|lf
tests/benchmarks/crypto/OT-094-OT005-CANDIDATE-READINESS-V0.json|7554|bb607158cbe8ac95a470f0a6c87fbb6d8d986259cf86540b14245fc1167dc7ae|lf|lf|none|lf
tests/benchmarks/crypto/OT-095-OT005-CANDIDATE-SOURCE-LOCK-ADMISSION-V0.json|7243|3fb904e1d5770613ec5d84560ea91dc3ec318a8a96c89c7d4333aa229267bab8|lf|lf|none|lf
tests/benchmarks/crypto/OT-096-OT005-MBEDTLS-STATIC-ELIGIBILITY-V0.json|14338|1a49125c3b236a5b744c0ca198e5a1f30b1509d9e58d86cce836f70fb1f10030|lf|lf|none|lf
tests/benchmarks/crypto/OT-097-OT005-LICENSE-AWARE-SOURCE-LOCK-ADMISSION-V1.json|3260|d9c77f2cd22200fa18f8f43bffccfa55123f57a4f73979f2c037768fcfb44427|lf|lf|none|lf
tests/benchmarks/crypto/OT-100-OT005-LIBSODIUM-SOURCE-LOCK-ADMISSION-DELTA-V0.json|3524|df595f2d07ba1b5d0a9bdf70237b1f0ea5a01fe8cb5a63ffb3575fe484faede0|lf|lf|none|lf
tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-ADMISSION-DELTA-V0.json|4422|98cce120cadc1bddf5851f1480ae181488e17277ba0a2c8c8c38a70a062be105|lf|lf|none|lf
tests/benchmarks/crypto/OT-103-OT005-EXACT-RECEIVED-TARGET-PROFILE-EVIDENCE-V0.json|6043|517809caf31250d126cc3619f9d05386a92811a594dca0087d9acbf1b671147e|lf|lf|none|lf
tests/benchmarks/crypto/OT-110-OT005-US915-DIRECT-RADIO-PROFILE-EVIDENCE-CONTRACT-V0.json|8864|8af36e000d5cd0478d1a829fb5a1f2b330cdf09bad188445d30579c348f7e2e1|lf|lf|none|lf
tests/benchmarks/crypto/OT-121-OT005-PHASE-TWO-EXECUTION-AUTHORITY-V1.json|6279|765aacd8a33862265b46da2d60333759cd96b72a8acae9e70b22c5bda2dbd90f|lf|lf|none|lf
tests/benchmarks/crypto/OT-122-OT005-LIBSODIUM-NOISE-RESOURCE-EXECUTION-RECEIPT-V0.json|5001|2b023c640bfbec8ad6eb5d1d63d65e8f1ad75dcfe593566aba6b3d468355a178|lf|lf|none|lf
tests/benchmarks/crypto/OT-124-OT005-MONOCYPHER-COMPARISON-EXECUTION-ABORT-RECEIPT-V0.json|2156|f638a6125a14d8fe28412ae0554c6958bdad3a6c2a0a1e83e3ae793bcad4e92c|lf|lf|none|lf
tests/benchmarks/crypto/OT-125-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json|2990|b76e6f420b44f1464e2e8f026d0495c7a7666ac0c99966d078c903a4011e8acf|lf|lf|none|lf
tests/benchmarks/crypto/OT-126-OT005-MONOCYPHER-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json|2717|247b0b80e64a3f6bf6654be279e90dcbd80a067c52ef861313a6f370c0355941|lf|lf|none|lf
tests/benchmarks/crypto/OT-127-OT005-MONOCYPHER-CORRECTIVE-RETRY-AUTHORITY-V0.json|3408|d043fc7dc700ce2c43914fe079b4a594b04e730252d8cb490f02097d9472448b|lf|lf|none|lf
tests/benchmarks/crypto/OT-128-OT005-MONOCYPHER-SECOND-CORRECTIVE-RETRY-EXECUTION-ABORT-RECEIPT-V0.json|2629|b34b4f761d77a5d952e50eda4c48d7629b5185043cc84c146dc9b974ba36f09e|lf|lf|none|lf
tests/benchmarks/crypto/OT-130-OT005-MONOCYPHER-IMMUTABLE-EXECUTION-BUNDLE-PREPARATION-V0.json|8623|cc1d88aa9f5e45c3b13a1f229b767fcbf8e7dd383a75309e335f5397ddc780f7|lf|lf|none|lf
tests/benchmarks/crypto/OT-130-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json|3505|278ace6ce442a5c5b9ff5baa525c7e647798ebeff45a2f376bff67c762b6cdb9|lf|lf|none|lf
tests/benchmarks/crypto/OT-131-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json|2349|4557149d6a45fb12bddeea968edea6f9d5ace76edf9d4203f13cf79711a6f489|lf|lf|none|lf
tests/benchmarks/crypto/OT-131-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json|3870|c134107cf98c75045e033cb087b58dce9e693e77542e1e70406a8896a2532eb9|lf|lf|none|lf
tests/benchmarks/crypto/OT-133-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json|2885|5a57675d1d367968fa1af20c97ab0a7ca4eb005a5b3f1fcf77c52fc552af6d04|lf|lf|none|lf
tests/benchmarks/crypto/OT-136-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json|4725|bc968110fce49e45352d7dfd884a9f8fb0d4a3a8a2ce4fa510087f97570e113c|lf|lf|none|lf
tests/benchmarks/crypto/OT-136-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json|4142|a890b570e088ecb941b3cd8a8915ba2950d99224963efaccf1b83c64e79296ad|lf|lf|none|lf
tests/benchmarks/crypto/OT-137-OT005-MONOCYPHER-EXECUTION-ABORT-RECEIPT-V0.json|2888|1f6a75e2941045eb3585161769bb2a3ae544b1192d04594dc9d9bec53d77212c|lf|lf|none|lf
tests/hardware/OT-059-2026-08-15.md|6650|4cce747a9b39354346efc638cde2c6851e17a0b6cc6fd3339302c3674a34ba9e|lf|lf|none|lf
tests/hardware/OT-061-2026-08-16.md|5227|fb5c9a00222cffecf7afc991866f5e86e8f21cc6fe92e1bf917dd1912644564a|lf|lf|none|lf
tests/hardware/OT-093-2026-08-20.md|7161|d4b504b34ec731b287ef24276861bd7ce105fd2b613ce00cdc3d22cb84ce8938|lf|lf|none|lf
tests/hardware/OT-112-2026-08-21.md|2514|7e2c6ffe263696fcaa71297b35f58177936ace011a2d2291ded910ceae40f19b|lf|lf|none|lf
tests/hardware/OT-115-2026-08-21.md|3849|4ea35e4b694133c7e3c7a3499c35da13916cfab1938b7186cf21bc4f2df5a175|lf|lf|none|none
tests/hardware/OT-129-2026-08-24.md|2632|f101211094047c65d7326ca3a796933105987f37cff7003d647553adfb79e866|lf|lf|none|lf
tests/hardware/OT-135-2026-08-25.md|3203|1f776274041d8ab9fabbe09ddd15d257ee55c13bb36d06a9ed205a5838ebe9d3|lf|lf|none|lf
tests/host/crypto_benchmark_baseline_tests.py|22742|83dff18b1b73f3567368e6e2cc582718eae7437e1a8a535c060ccaa1634730d4|lf|lf|none|lf
tools/Build-HeltecV4BenchTarget.ps1|48249|6f3ffcb724e2eb52f3d553596481983330fdb2484871b0aab5f793763f2d07fe|exact|mixed|none|lf
tools/crypto_benchmark_baseline.py|26497|84e441141708d839d6cb13117476068a7c36570fdafd880173196205c778c747|lf|lf|none|lf
tools/ot110_radio_runner.py|41789|57c2685149ddb5d8e569d0067f71201c33163b7deb52f1a9d24944132ca20947|exact|mixed|none|lf
tools/ot123_monocypher_frames.py|12235|2276aab6246898186804d39b08be342989ad2cf2c804b546b43cbab31350a721|lf|lf|none|lf
tools/ot123_monocypher_runner.py|35200|f6c4070512d1c8d0d58bd386646f1e4098084b275610a7834dbf0ee0145eb1d1|lf|lf|none|lf
tools/ot125_monocypher_retry_runner.py|36683|47022c46ce6d911998b5457516e250e3dec7dcc8dbe1e0c8e799a0cacfc23150|lf|lf|none|lf
tools/ot127_monocypher_retry_runner.py|38209|ff81188b1f211aaf504192d3827147b2f572b29e895e45eeeee1bc505ffb5438|lf|lf|none|lf
tools/ot129_monocypher_protocol_runner.py|10593|f95c5e673a698dc1392de08611abd3cb38f63dec45423ae2100f2f59265e9c9e|exact|mixed|none|lf
tools/ot130_monocypher_coordinator.py|25904|5487fd1ef7a8fffcd53a1fdc8d8d81b9459c127fcff2d4dd4c9ea8804431e8b0|lf|lf|none|lf
tools/ot131_monocypher_hardware_adapter.py|12854|fc2012d5aaa13e53c70ff9c68059231e2c4945840435d4add49a22a7081e1b96|lf|lf|none|lf
tools/ot133_monocypher_coordinator.py|25906|04d050d540f49203f2d041cb87dfa4f0d8f741aa268d43b75866ce67a4458cff|lf|lf|none|lf
tools/ot133_monocypher_hardware_adapter.py|12850|1dd32656743a4ec3486cab110158d07824b8f2c0cbda2b4e2a8dd8420187086a|lf|lf|none|lf
tools/ot135_monocypher_protocol_runner.py|10601|e06fa00ccef1aeea167286698d64bdd546a09406921dd074009d7174a5366993|lf|lf|none|lf
tools/ot136_monocypher_coordinator.py|25904|3d340194f98b9d99d7833510d19b142e660ee2999d2d4e20000ca13d7f380867|lf|lf|none|lf
tools/ot136_monocypher_execution_authority.py|22790|47bdeb66fa1cf25abb7e09cf89bb21b8dce2f88759ca3ab4fd2fa767f8c57eb9|lf|lf|none|lf
tools/ot136_monocypher_hardware_adapter.py|12848|73a3cfd606ae2249c1920b27fa94daccecde20154f8b484de03fdd003c13aba8|lf|lf|none|lf
tests/host/ot135_monocypher_protocol_runner_tests.py|11608|5af2692780026fc4d80225e02f76831ae1229dbd4d9cbe8997c8f2f252c524de|lf|lf|none|lf
tests/host/ot136_monocypher_coordinator_tests.py|18693|ec9ec994b3399f55a3f2d598fcfe9bb6e7f0d3a5805c6c34af4a758104456c80|lf|lf|none|lf
tests/host/ot136_monocypher_execution_authority_tests.py|14802|f5bc7b909aad15c6cf0033d6b53fcd4ba21e5294ea9665b42bfc9262ceae7612|lf|lf|none|lf
tests/host/ot136_monocypher_hardware_adapter_tests.py|12361|f4c81a88e99857062a097c4406d10a26483f3085ca606b96cace137de6d332b6|lf|lf|none|lf
firmware/targets/heltec_v4_bench/main/companion_authorization_storage.cpp|12764|308676a74b019b5fae2fa802503487b652f2d6aa060eab5a407098728a3a62ec|exact|mixed|none|lf
firmware/targets/heltec_v4_bench/sdkconfig.defaults|1409|a747ed37ec7be4dd1199f52af43395ff58ac92f897b2c35ac73b0a0ed6cf6ecb|crlf|crlf|none|crlf
firmware/targets/heltec_v4_radio_diag/main/app_main.cpp|24945|66c5b8cab273556802a3f04c11a85c2c72b02df6d4e4685e2314a4eb0c9a0f2a|exact|mixed|none|lf
tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/dependencies.lock|543|d5f32dcb2ec24c853ac2040fb8f9410a8f61831b7b7d5e9522766e8456e2ec5e|crlf|crlf|none|crlf
tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/patch-manifest.json|44|4b2b01f1561c0e55d0461388f788535b4842d28684b205cc3bbf91b40575100e|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/sbom.spdx.json|1450|86e76598aa0a668239ae86e515b6e9a53fd03682e3a9b3325fceb19043780df2|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/espressif_libsodium_1_0_22/source-manifest.sha256|87846|988fbb2b95d1b4f3850d153495f8d73482be52cb80808fa48e24a8d1e9bf91ea|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/libsodium/partitions.csv|452|4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389|crlf|crlf|none|crlf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher/partitions.csv|452|4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389|crlf|crlf|none|crlf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot129/partitions.csv|452|4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389|crlf|crlf|none|crlf
tests/benchmarks/crypto/OT-098-OT005-EXTERNAL-CANDIDATE-ACQUISITION-V0.json|5454|b7be03e305c6253e10f69f624132a736cce5aea3f559760cde4f948ae79abad6|lf|lf|none|lf
tests/benchmarks/crypto/OT-099-OT005-LIBSODIUM-MANAGED-IMPORT-V0.json|4982|8285fa7308bfc83a5d55503a7a3e1fa4c21895a42b095197b3ec75f634411ec9|lf|lf|none|lf
tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-BUILD-RECIPE-V0.json|5948|c109392296cf313f276bf0121629eeec2db8bf75773f76ca8086fa2f1a04e5cb|exact|mixed|none|lf
tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-CANDIDATE-SIZE-REPORT-V0.json|2045|56bcf252c5392326ba07376881c960d76058750d933812da581caee337b02ac4|crlf|crlf|none|crlf
tests/benchmarks/crypto/OT-123-OT005-MONOCYPHER-COMPARISON-PREPARATION-V0.json|7951|a80f06c4b6c0c1c56b5b36ae54b8fddacf36359b8b69abe6f0f4da2bd5d18a89|crlf|crlf|none|crlf
tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-CANDIDATE-SIZE-REPORT-V1.json|2047|7978147ea7e8197fc852d65650e44d2fea134799caa2331e27ae87fb2970f24e|crlf|crlf|none|crlf
tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-CONTROL-SIZE-REPORT-V1.json|2045|95743702c91609fb53cd3ad7020ab76120fa9690bd4921b3f73dbbb3e3f5b2d8|crlf|crlf|none|crlf
tests/hardware/OT-132-2026-08-24.md|2669|2f7ee064f55446c33556c168e14dadb3ee7c023616b5860b796c62b048540b59|crlf|crlf|none|crlf
tools/ot132_monocypher_protocol_runner.py|10942|d131286969d82f0ddef8b3051b6d64588042a4d49fafa2aca1d164de617f6a3d|crlf|crlf|none|crlf
docs/decisions/0078-accept-reproducible-monocypher-quiet-target.md|3084|9f75b8059580f8b7a70d960778df43f9bdf18160dd777d8152de4dadae5b6998|lf|lf|none|lf
tests/hardware/OT-139-2026-08-25.md|4020|b21122026af9e12bd1b6db408b1b3e0d2977ca86a1d2346fcfb0723a1fcb8a95|lf|lf|none|lf
tools/Build-Ot139MonocypherQuietTarget.ps1|11812|2348c73e965aa3bba3c55fa96ee14d5e9cf405d3d513cf3901103d6b2090d3dd|lf|lf|none|lf
tools/ot139_monocypher_quiet_target_evidence.py|12992|a49dc4098f32116c4ed9a0caa40dc3d0d937ba8d9ad0a876899eabd4b47c543a|lf|lf|none|lf
tests/host/ot139_monocypher_quiet_target_tests.py|7390|640b83c7ad3355eaad846fc9b0a11b8b9db44d69f1a5ad5c15307c1a93eccffe|lf|lf|none|lf
tests/benchmarks/crypto/OT-139-OT005-MONOCYPHER-QUIET-TARGET-BUILD-EVIDENCE-V0.json|11714|1a1aa548ed6487a56529e6b75cee253f707e35926b69b6d1e1effcc47287561d|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet/CMakeLists.txt|234|943eff10472e939007b7c0bfccf51f453a18478845be5b4318ffee639bcec5db|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet/main/CMakeLists.txt|895|0085c4f900c2b37003bf55d719cb7fa9980198f0a252723296c9b50af0409f8b|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet/sdkconfig.defaults|997|f8d20cdc61ba606e47ba76049b7be97d959441abea691deae3b85cba7fd2e404|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/monocypher_ot139_quiet/partitions.csv|452|4f064c125aa641697e0539eaf9eda9d1cdecab46dd8ff387988b900f3efe2389|crlf|crlf|none|crlf
docs/decisions/0079-freeze-ot140-quiet-target-bundle-and-authority.md|3716|be56fc505f64141f8852dd12bc0d374d3f98a7ba5ef65af0d5c93582cb28da61|lf|lf|none|lf
tests/hardware/OT-140-2026-08-25.md|6060|acbceb0b80e47d1a5256974cde06793b66f73a1ceba8029d24d4eb705e7169c2|lf|lf|none|lf
tools/ot140_monocypher_coordinator.py|25905|a69cd1d5b0abc71b4dc786ad734ed977584790de88eac3a96d7086ec15dbeb16|lf|lf|none|lf
tools/ot140_monocypher_execution_authority.py|25469|d3da7736c2e03d7dcd854de1f290eebb4ea07e1655089ba40dd5504f8d460221|lf|lf|none|lf
tools/ot140_monocypher_hardware_adapter.py|12851|3001cba006d22e62a9941ebc6a07768731b589245ca2b53ba7bb53f4578cc1b7|lf|lf|none|lf
tests/host/ot140_monocypher_coordinator_tests.py|18693|a5462de95018de0dc3cad3814dbcd66f4db2495ad2a31de8e5963e5d19cd0f60|lf|lf|none|lf
tests/host/ot140_monocypher_execution_authority_tests.py|16448|da1c3030ef1abfd970ff69c6b9a47bb81fe31a3dcf65497c35910b649b45574b|lf|lf|none|lf
tests/host/ot140_monocypher_hardware_adapter_tests.py|12347|8c734cc51c41a8f5e853e029a8e3f1c118173e6e98bf31ccc7d7d201a0a9b87e|lf|lf|none|lf
tests/benchmarks/crypto/OT-140-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json|5982|523ecfcd17f2fad2687622c0e0d062d4c5710551ecb3a731b91298e7474704eb|lf|lf|none|lf
tests/benchmarks/crypto/OT-140-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json|4333|fa867f3551b069767d2b00187841dc60cf861ee38492a1958ef86ed98398de9a|lf|lf|none|lf
tools/Build-Ot143MonocypherCorrectedTarget.ps1|15490|ae569d634b13f70d747255648e7f6ef11eb95165ca5a43d61e0bf37d9b511974|lf|lf|none|lf
tools/ot143_monocypher_corrected_target_evidence.py|15887|b7f1d35e36007998056669509fa9be0364eeac4c2043ccb0bf98bbd068a016ac|lf|lf|none|lf
tools/ot143_monocypher_coordinator.py|25906|937f37a01beef9df8eb327c741ae7453c3f3d565c02977f7ae7ff5711f48579b|lf|lf|none|lf
tools/ot143_monocypher_execution_authority.py|26356|5d860fc950b6983aa92d41552b968977ea9659970f7a16df1e111254c3830f15|lf|lf|none|lf
tools/ot143_monocypher_hardware_adapter.py|12852|d7ed9e8c2d48f5979b6e7160ab90be2f892860a3542c561f259df199ed49b843|lf|lf|none|lf
tests/host/ot143_monocypher_corrected_target_build_tests.py|10880|95150e40d773b564d0f1d18ef12ed06cb8afa98b23f664e64b6730902c7b41a1|lf|lf|none|lf
tests/host/ot143_monocypher_coordinator_tests.py|18694|e7bf9ec139720bf0ee92d4c4bcb2d2abf63594f4aeab8f80459103cf87cdf106|lf|lf|none|lf
tests/host/ot143_monocypher_execution_authority_tests.py|17863|4b6fcc1d5683e0f5ad779600015b1a0fbfe0cd889aaa45d4f4d0c50cd97f270a|lf|lf|none|lf
tests/host/ot143_monocypher_hardware_adapter_tests.py|12348|a63eb9aee89db7f2b962ecf65560b507dcac239d81915ef01bbc6a8b4ea8a484|lf|lf|none|lf
tests/benchmarks/crypto/OT-143-OT005-MONOCYPHER-CORRECTED-TARGET-BUILD-EVIDENCE-V0.json|11794|1045d5d59c26775b8a8c2a8520226fcb224b566f5554b5ff495b9876a8af8c37|lf|lf|none|lf
tests/benchmarks/crypto/OT-143-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json|6231|b923059493a48fc11cee6393e6b0a17e1eb119a429b2a8af95dbc0b7ed03ddc3|lf|lf|none|lf
tests/benchmarks/crypto/OT-143-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json|3708|235e1227ef8ebed75a510335b017f9c0539d0508426a11414546591002f1bce0|lf|lf|none|lf
docs/decisions/0080-freeze-ot143-corrected-target-bundle-and-authority.md|3981|c6d3249e7b646800da49d148e26b28f5f9ad965a78998ee5a9ef64f12176f8ca|lf|lf|none|lf
tests/hardware/OT-143-2026-08-26.md|6049|b855199b87fea9e5666bfeab1cc0b458ed34f7145b9eea5f9b9002a98202ec40|lf|lf|none|lf
tools/ot144_monocypher_coordinator.py|25912|2e8955c7a425c33208d8b6e944fdef52259cd146274bec44c543e1f4b7af69be|lf|lf|none|lf
tools/ot144_monocypher_execution_authority.py|26519|d0598b529438d0b37671d65fd6c17d97791d2ab564806b378203e2834ecd1a52|lf|lf|none|lf
tools/ot144_monocypher_hardware_adapter.py|12854|19eb64c29deda5af9e4d523a8bc1630ae2f9350cba4043e35ac7691146b09b71|lf|lf|none|lf
tests/host/ot144_monocypher_coordinator_tests.py|18700|6cfa76222e93a71b595db733485372c7751f602b9b161e5400d896c2bce171eb|lf|lf|none|lf
tests/host/ot144_monocypher_execution_authority_tests.py|18185|fdee3506b432dc042cc195762ae89e3f2cbb636f763799d54bbd3ea185dc2a9e|lf|lf|none|lf
tests/host/ot144_monocypher_hardware_adapter_tests.py|12374|72717b63bf349ceb3b99d831f2fbea6c3127d3cdcaeed40fbdcdff8c57410282|lf|lf|none|lf
tests/host/ot144_monocypher_binding_consistency_tests.py|4613|762afe57359aedabe9aba02e27444ae457d3b1887a8410b0e047306b0a2c0c07|lf|lf|none|lf
tests/benchmarks/crypto/OT-144-OT005-MONOCYPHER-EXECUTABLE-BUNDLE-PREPARATION-V0.json|6298|9736e6c68647da594e31b8c32e076fd40c2fccedd1bbd1d740b5cc0443667455|lf|lf|none|lf
docs/decisions/0081-correct-ot143-runtime-binding-before-hardware.md|3319|cd5b027945e44abecc24b1957301f40a2d203e98802014e0a005582e9b4958c2|lf|lf|none|lf
tests/hardware/OT-144-2026-08-26.md|4174|fb3355f5df9b3fc5e4817c56badf09eb292dd981cbc528241ce3c247f74eb182|lf|lf|none|lf
tests/benchmarks/crypto/OT-144-OT005-MONOCYPHER-ONE-ATTEMPT-AUTHORITY-V0.json|3775|29c84ae8de494be2ac08e35cdeba5c4409f9381bc4f457a29cef64fb391afe31|lf|lf|none|lf
docs/decisions/0082-accept-ot145-one-attempt-authority.md|3194|a3182e8db3349a16f7526661bdbc37552801ea82c5ca143677c1cad53479d12b|lf|lf|none|lf
tests/hardware/OT-145-2026-08-26.md|3930|3dde9529340c055f4fc06191df9b36ea39bf5c49f35de2e8ec87e30c27091e75|lf|lf|none|lf
tests/benchmarks/crypto/OT-146-OT005-MONOCYPHER-EXECUTION-RECEIPT-V0.json|6567|9a5ea09fa8cdf465f5c83b6a0eb69fb80579806b0635ac63083e1234e4f91464|lf|lf|none|lf
tests/host/ot146_monocypher_success_record_tests.py|7817|968e1836f876fc595630d361324942b63c99008a25e2566599ce4e5eb5f6c876|lf|lf|none|lf
docs/decisions/0083-record-ot146-ot145-monocypher-execution-success.md|3042|7f625f3ab2eaa8c84dc22cfdf5aad29839f65d67058fa43e73dfd97c2abf55c5|lf|lf|none|lf
tests/hardware/OT-146-2026-08-26.md|4540|1f899dc57d3c578efc7aa02c211615957b9fe07a6943812f769e20c255daa788|lf|lf|none|lf
tests/benchmarks/display/OT-147-HELTEC-V4-LIVE-STATUS-BUILD-AND-FLASH-AUTHORITY-V0.json|2838|a367ce3a13a89241aa6bd55c2ffec4adb17c15262e6c640c1608b2e43b8a43c5|lf|lf|none|lf
tests/host/ot147_heltec_v4_live_status_authority_tests.py|6646|35a17e61edb44a594d95ec1dd6e2420a2ad3adb2388249b9bf8ed536535a7e6b|lf|lf|none|lf
tests/benchmarks/display/OT-147-HELTEC-V4-LIVE-STATUS-EXECUTION-RECEIPT-V0.json|2151|165893b72537b04da210e1dbec268b7681f98c2a8aa0ba6bcf0906c3a28897cd|lf|lf|none|lf
tests/host/ot147_heltec_v4_live_status_success_record_tests.py|5387|cb07d6eed2d77a823ed42dca8c5bbcf12c2756627ba9366244116b513aa22476|lf|lf|none|lf
tests/hardware/OT-147-2026-08-26.md|3192|01b6f695485f50ad56dafdef6939ab668465de5483b7d72ba7df97e3cbfc01dd|lf|lf|none|lf
tests/benchmarks/crypto/OT-148-OT005-PHASE-TWO-CORPUS-RECONCILIATION-V0.json|6249|beddc729f8449c3f2e3a09f62ba6947312f4e1893eee38abfa7a9a616f1bae1c|lf|lf|none|lf
tools/crypto_phase_two_reconciliation.py|10959|f4fa81e2633d2e8da6109b129f53b686f45725da5ade9cab34c8dd161533c8b2|lf|lf|none|lf
tests/host/crypto_phase_two_reconciliation_tests.py|4179|17b7cb9b1909b237f0065a41031e86b6c4802abbd10c257462d8cc5749edeb97|lf|lf|none|lf
docs/decisions/0084-reconcile-incomplete-phase-two-before-selection.md|2958|8568a15f515fffd63fad0a465e5e98196e629e5e43c759997dad91143a368201|lf|lf|none|lf
tests/hardware/OT-148-2026-08-26.md|1443|82def086ad86b262d3117d48bb4d6fc578861d6cdc2339b9e48f4f1718172eb6|lf|lf|none|lf
docs/decisions/0085-prepare-mbedtls-psa-target-and-resource-successor.md|3102|5fa74f34f8e111eed0d773fc3d31dfaaae35c9861b8306d8b48919435185c41f|lf|lf|none|lf
tests/benchmarks/crypto/OT-149-OT005-MATCHED-RESOURCE-ACCOUNTING-SUCCESSOR-V1.json|6631|575efa5700bf2d2e40cf3fb49c0b9f860815dddc3ea81bb5a54a3d91733339bd|lf|lf|none|lf
tests/benchmarks/crypto/OT-149-OT005-MBEDTLS-PSA-TARGET-PREPARATION-V0.json|10546|49d1cfba5fca01afeb1928be922eec287873b85b1d517c81c249e148331b8867|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot121_candidate_benchmarks/mbedtls-psa-result-frame.schema.json|7512|f24496e3151a725682838e791810b41401bd06e36db6ad9dbca89e9ee7aac2dd|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/candidate/CMakeLists.txt|1528|be533296c54c2a0d91c6f0c1c5c8f10197b96886fb5d6cac04e31ae0344474e2|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/candidate/main/CMakeLists.txt|595|1af87f0a96fdadabe66c0d5bc3daeed5843f8a7ae03398b80a98954bec8da3e2|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/candidate/main/mbedtls_psa_benchmark_api.c|16483|06cf13b01d479c39c52e480315ad69732a76c760978541b2ac6b207997a36673|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/candidate/partitions.csv|443|973ce7d2d3559a792d62eacd859db1b52b7569080cb85c3f2fedeed4db6cc621|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/candidate/sdkconfig.defaults|390|e8e4c1939008422d21dcd6993d3b3e6d3dd0d53328b46c4a5649188dc60014c6|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/common/app_main.c|9044|b8c66b8dfe0877ab715fa611fc42e18eba2308b582ac70ef18dbc2e3fcb42e5a|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/common/ot149_candidate_api.h|501|c086022e8a6499495b2814619f0a4f3a8a70410870dc4ee193fc5105056adea9|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/control/CMakeLists.txt|1528|be533296c54c2a0d91c6f0c1c5c8f10197b96886fb5d6cac04e31ae0344474e2|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/control/main/CMakeLists.txt|588|c49f34f5ff690c41a75780f931af10b42d8a402b696ec047cd7a9707fb1dff80|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/control/main/no_candidate_benchmark_api.c|1121|a3f5d29bfd925a4171eb8e1d0aab13c0dec02419c62be362db9373662d5c1035|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/control/partitions.csv|443|973ce7d2d3559a792d62eacd859db1b52b7569080cb85c3f2fedeed4db6cc621|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot149_mbedtls_psa/control/sdkconfig.defaults|390|e8e4c1939008422d21dcd6993d3b3e6d3dd0d53328b46c4a5649188dc60014c6|lf|lf|none|lf
tests/hardware/OT-149-2026-08-26.md|2079|f4e176d86c8cd10ff9f441b883b167a392e235abd444429927a0370e2c0ab9b8|lf|lf|none|lf
tests/host/crypto_matched_resource_accounting_tests.py|19450|28f5e902a3863e00af66e14925cfa8a48e6c276bf83dd394012e5729b6856c94|lf|lf|none|lf
tests/host/ot149_mbedtls_psa_frame_tests.py|16402|245fd33de1d4b8df76d7a1d9375f02d97f40f0da9dee02a6fb4c17bf30e9d023|lf|lf|none|lf
tests/host/ot149_mbedtls_psa_harness_tests.py|14023|9fddc8a0c77f1462197a475ea0013f389fab624fece058c0d9ee9dd2099a0dd4|lf|lf|none|lf
tests/host/ot149_mbedtls_psa_preparation_tests.py|6159|2c0d0da622000e0669e5784beab74da4742cde3f5b373b431f07274dee17c38e|lf|lf|none|lf
tools/crypto_matched_resource_accounting.py|32260|54ac45f5676605c767cfa6144d75e67c12aa919228491d68967c8a7d53910b7e|lf|lf|none|lf
tools/ot149_mbedtls_psa_frames.py|12472|a431e45c8ab2098d6973171f4c325c7f5d7e2b6f183e01b3422086dbac6d15c9|lf|lf|none|lf
docs/decisions/0086-freeze-ot150-mbedtls-psa-executable-resource-bundle.md|4433|55af2ed72393cef16311119f33003156d2f506504b8d8937cfa5c66b1ab40d21|lf|lf|none|lf
tests/hardware/OT-150-2026-08-27.md|4162|2d5b235d40a42a4d28c8b419666d31b0bfc334d03c62d5a9b9fdd6d48642a02b|lf|lf|none|lf
tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-EXECUTABLE-RESOURCE-BUNDLE-PREPARATION-V0.json|9680|6f5a1e84acfd9c08f5201e84ab0ff18d12b18e3ab82141211d8f56f0a9e21072|lf|lf|none|lf
tests/benchmarks/crypto/OT-150-OT005-MATCHED-RESOURCE-RESULT-V1.json|9590|14ce18688f4fe9e2d819666c857e07843a86cc694a23662832476c6f9cb0b7c0|lf|lf|none|lf
tools/Build-Ot150MbedtlsPsaBundle.ps1|22988|a57c6e340f289f8b6c40f4f0d501648229aa71e3a4a29007f38040c45b5f69f4|lf|lf|none|lf
tools/ot150_mbedtls_psa_bundle.py|20055|241c249047ac6b1daca6f210ae68ce3b51cfb218635d5039c1649e64eddde3c5|lf|lf|none|lf
tools/ot150_mbedtls_psa_coordinator.py|26440|1602d9bf1a306a1b88c701bebd8cff920adf75d80533f09baba5de7b72144b97|lf|lf|none|lf
tools/ot150_mbedtls_psa_execution_authority.py|27115|e80f0fc978fc19110281de25fdd0822a67b09eb31d4d66ed37e3641bf69c7fc9|lf|lf|none|lf
tools/ot150_mbedtls_psa_hardware_adapter.py|13213|8f953e1a9b633a9ac3c443ab47654545cafa1a395e835673661162668228313e|lf|lf|none|lf
tools/ot150_mbedtls_psa_protocol_runner.py|10599|82c2d8d39220e41d8f0e69bbbebfbcd30929ee0a01a692b996319a9bfdbcba92|lf|lf|none|lf
tests/host/ot150_mbedtls_psa_builder_tests.py|6752|22c986d5b0f4157003434c6a7a3f8bc9223de4b4f79a18a4c436c84f69431d6f|lf|lf|none|lf
tests/host/ot150_mbedtls_psa_bundle_tests.py|16992|be29df3ed5302fa7e90db3330ede34fea90eba6bdee276572b78a6e583e17453|lf|lf|none|lf
tests/host/ot150_mbedtls_psa_protocol_runner_tests.py|11623|d88f3c01683dea60c1c6f96a62e5ba581e979be713bb2c839618000bc8f469cc|lf|lf|none|lf
tests/host/ot150_mbedtls_psa_coordinator_tests.py|19887|7c15cba3d8e4de5c76551f84a72cf0371f54f28e90cc1507f32a071cc3d80f26|lf|lf|none|lf
tests/host/ot150_mbedtls_psa_execution_authority_tests.py|17305|b2053d120e1b9c3fef54f1dce01088bbf9943655030acfb582aa7fe9e562df73|lf|lf|none|lf
tests/host/ot150_mbedtls_psa_hardware_adapter_tests.py|13495|50fd9dbbfb3028c317895630a6c8711572f1ebe3ee6d21a9b6f9fe2fee2e7008|lf|lf|none|lf
tests/host/ot150_mbedtls_psa_binding_consistency_tests.py|5608|6063432aeb2d2eed36f3cb3d0432ada70eef9aad23c937ed6e9d11bf9c5034a6|lf|lf|none|lf
docs/decisions/0087-record-ot150-abort-and-correct-mbedtls-psa-successor.md|3689|0ec3716f2d7804b2368665fb6113968cd365e8e5be777fdafc94d34428fce20b|lf|lf|none|lf
tests/benchmarks/crypto/OT-150-OT005-MBEDTLS-PSA-ONE-ATTEMPT-AUTHORITY-V0.json|2911|cd5e93cae7a3bd1f65b41a6a69a97523798e8c64ab1112618f5c77fec5515362|lf|lf|none|lf
tests/benchmarks/crypto/OT-151-OT005-MBEDTLS-PSA-EXECUTION-ABORT-RECEIPT-V0.json|2754|b71e2881883789965c7680c7cc4d854a66725900445f790791a10140ffe926fc|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot151_mbedtls_psa_corrected/CMakeLists.txt|1553|2b21a883874515ad9b154fac15114bbac9d754b50799d40fc6ffc64244c31921|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot151_mbedtls_psa_corrected/main/CMakeLists.txt|595|1af87f0a96fdadabe66c0d5bc3daeed5843f8a7ae03398b80a98954bec8da3e2|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot151_mbedtls_psa_corrected/main/mbedtls_psa_benchmark_api.c|16460|acffeafc8a51d5bbdcab515abee05bb3ccf8e3368b41f0acc3a0d78b73ced209|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot151_mbedtls_psa_corrected/partitions.csv|443|973ce7d2d3559a792d62eacd859db1b52b7569080cb85c3f2fedeed4db6cc621|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot151_mbedtls_psa_corrected/sdkconfig.defaults|390|e8e4c1939008422d21dcd6993d3b3e6d3dd0d53328b46c4a5649188dc60014c6|lf|lf|none|lf
tests/hardware/OT-151-2026-08-27.md|5978|74a7f58b011f3db4f3edf6b002aabaa6c59601de4573789f354452055e164754|lf|lf|none|lf
tests/host/ot151_mbedtls_psa_abort_record_tests.py|5436|43740eb0767b44fd9648648d5aa5ee92f908e4554abd647cc58ea17f71249c60|lf|lf|none|lf
tests/host/ot151_mbedtls_psa_corrected_target_tests.py|3123|b29443bb543a910afe97f02ff45724cc0a1605821442391b4be9300463b32341|lf|lf|none|lf
tests/host/ot151_mbedtls_psa_failure_transcript_tests.py|7586|2029a5e2db11e280732e073dd997a05207c3d8d3c87ef3f1bbfc5c96dd66ae50|lf|lf|none|lf
tools/ot151_mbedtls_psa_failure_frames.py|8882|3fb9fa51340a355848da45d06f66702efdadce3d265afc679bcf5407dbb386cb|lf|lf|none|lf
tools/ot151_mbedtls_psa_protocol_runner.py|9880|46a4d978d92a84437d4d93403e67d27664009dc7f0be4c66c961685d94ed430b|lf|lf|none|lf
docs/decisions/0088-prepare-noise-xk-radio-cost-measurement.md|2846|40fe9d0cdeb07ee76ac2aaabda584b16aa9f7e9b5f044b26ea793d07bd8e1c8f|lf|lf|none|lf
tests/benchmarks/crypto/OT-152-OT005-LIBSODIUM-NOISE-XK-RADIO-COST-PREPARATION-V0.json|9258|f556e9c70a4e46afc12d4ff7cbfd3ea8ad95f9b3055313a1ffbbcefe21611fb4|lf|lf|none|lf
tests/hardware/OT-152-2026-08-27.md|3455|72d3b68d13f0c1684533565f101bb04f92926033e64faca8607e5ce66a30377b|lf|lf|none|lf
tests/host/crypto_noise_xk_radio_cost_preparation_tests.py|5911|c629e50a7be05f14ab6b823b8a9f384a690c9a4776b95323cbc04d32e9ada4d5|lf|lf|none|lf
tools/crypto_noise_xk_radio_cost_preparation.py|20973|6920cd3c8ed3f2d3888db4cb820bc315c62cb35e4033be9b19c270f6ad7c7c5c|lf|lf|none|lf
docs/decisions/0089-freeze-noise-xk-radio-executable-bundle.md|3563|d89889efec7b0f25419418a3de20f512dce345fce6a2d18851dc4411c84faf4b|lf|lf|none|lf
tests/benchmarks/crypto/OT-153-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json|7496|84d7ba8c601f59115fe5d7c74275dc8ccba807a10f87a14ee5afbbe40b3a184b|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/.gitignore|52|cf2bd54e1c982f5a8808a076b9e24e2d6d3a25b1c4b1df88cb29ba7911b86b90|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/CMakeLists.txt|533|3002fc04ae7c7d8cf70cab96e4ca7015754b37eeca442ad3892130548a97e0cd|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/dependencies.lock|515|1183b5498c32e74381f387f7b2a7dff4230f9fecdb18c46bf04b87b16c0333b6|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/sdkconfig.defaults|397|7a71b7b56aa665bdd6c927e4e4682fb0a5458b34557976777a5dc9b537a599d3|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/CMakeLists.txt|896|2bbdcf0cfd94ee1b74c89ca5e17e224a35a2caccb5d9a9e3174fe448fb6e3139|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/idf_component.yml|61|3618d4c90b69cd6b4fbfe7a45efe087648be861f6544ced88d96e5b2978c2a1d|lf|lf|none|lf
tests/benchmarks/crypto/esp_idf/ot153_noise_xk_radio_cost/main/app_main.cpp|38207|612c892ecf649657e9a1f5fb68f8e24d088c2d4a94841f2ac5a8fff93d5b4240|lf|lf|none|lf
tests/hardware/OT-153-2026-08-27.md|4178|62e8c589b558a933c5c6b0625c50fcd0fd631ecf075f62f35bc615a0fca9c1ea|lf|lf|none|lf
tests/host/ot153_noise_xk_radio_bundle_tests.py|17102|4082bfc11945b405c52bc972cd7e1c59f72a0b195a7935543a981ff9834ecbfb|lf|lf|none|lf
tests/host/ot153_noise_xk_radio_coordinator_tests.py|18294|d8964a0eba6646d8d32c6d1ac7098aef0c8e3fa58da0479f1d2a1cd3bb83db28|lf|lf|none|lf
tests/host/ot153_noise_xk_radio_cost_target_tests.py|11892|75500458f1317b2e2964a01160d580344864749960e480ac5463d47ab662d961|lf|lf|none|lf
tests/host/ot153_noise_xk_radio_hardware_adapter_tests.py|12358|2fc3a844877c7b922d1bd0f8bad45c06fdb8897935e57ca7c5200581d9239ade|lf|lf|none|lf
tests/host/ot153_noise_xk_radio_runner_tests.py|18754|1388c3d290d186177ff943b62a389c19020cffdb68edf4bb76b6772177a09e90|lf|lf|none|lf
tools/ot153_noise_xk_radio_bundle.py|20452|fc2ff341c882417e4d6f36f43241b84ede68330b5f5ea6bfd022fc67ad8e0a9d|lf|lf|none|lf
tools/ot153_noise_xk_radio_coordinator.py|27295|6635c73c6952b322ec1d72043a80f637b2cc04b70c1763f578ea4f38559aeaf3|lf|lf|none|lf
tools/ot153_noise_xk_radio_hardware_adapter.py|17460|d84aa9a1c0556f6421141a25336a3e87dab054cc971722b3f8058fe0a254f94b|lf|lf|none|lf
tools/ot153_noise_xk_radio_runner.py|29335|8b20512bf25f06247bb59defa092b8db82fde753484a3936d9e4aee2fba808be|lf|lf|none|lf
tests/benchmarks/crypto/OT-154-OT005-LIBSODIUM-NOISE-XK-RADIO-ONE-ATTEMPT-AUTHORITY-V0.json|3330|d792bd6814d03a196f74fdda5a675386e74b02e7e65335892a57f51a7dc5beb0|lf|lf|none|lf
tests/host/ot153_noise_xk_radio_execution_authority_tests.py|13812|e502ad0a4f7ff5c8a2a53bf315e71a1b26f8c327eb038c04c9154824d156d6ee|lf|lf|none|lf
tools/ot153_noise_xk_radio_execution_authority.py|20669|2218d33c7e879c98faedd53f23375ea4885dfd08075a8cc0e34925f63191d4f0|lf|lf|none|lf
docs/decisions/0091-record-ot155-noise-xk-radio-execution-abort.md|3398|0118cb4b9a84e2842c88fe06c7d949e5dec143833f146cffdcccb3379bc24598|lf|lf|none|lf
tests/benchmarks/crypto/OT-155-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTION-ABORT-RECEIPT-V0.json|2860|ae18a4bcc6f43b227ea7169dd3b77828d16440b7ca8216e64d0f4d39bf57d8de|lf|lf|none|lf
tests/hardware/OT-155-2026-08-27.md|3793|261387375eb60a60eedf7f3330356f49989703c1916ff693d34ba0fa3479aa87|lf|lf|none|lf
tests/host/ot155_noise_xk_radio_abort_record_tests.py|4353|1ebab659c549cdf6ee1133a89b575edc1e760224de2cdfe15f148515a8e05e5e|lf|lf|none|lf
docs/decisions/0092-accept-ot156-reset-aware-noise-xk-radio-host-correction.md|4321|da33122ab2dcd52712f7859565a74bdde039c289e20c6c475872493fe6963647|lf|lf|none|lf
tests/hardware/OT-156-2026-08-27.md|3931|60fdfaba7fa2a691202798cb9aaabdfc31f2f37ad5a2d0f37cafaef7733e7509|lf|lf|none|lf
tools/ot156_noise_xk_radio_runner.py|11099|81d0a329d34c20e76362b9a3f07221b77b77f02189dd06660119a02ac1700244|lf|lf|none|lf
tools/ot156_noise_xk_radio_runtime.py|6588|bcdb1a772971aa665be699c2699a2b69625c4e8bd2352abd21811be0a4295dd8|lf|lf|none|lf
tests/host/ot156_noise_xk_radio_runner_tests.py|10202|34a15186a9821650dc1b1e3e561b03a32f0ebe1996fb3657962126b4473b0832|lf|lf|none|lf
tests/host/ot156_noise_xk_radio_runtime_tests.py|8782|79de8553628426ca2d0eff61cf1cc9d723a5ef61a425c3116a5499671bba6306|lf|lf|none|lf
docs/decisions/0093-freeze-ot157-reset-aware-noise-xk-radio-executable-bundle.md|3766|6a927714662ae0993307dcd41ee89d83badf40d52ad73fd0cc4bd45f6342b4c2|lf|lf|none|lf
tests/hardware/OT-157-2026-08-28.md|3402|a1bd7aff8f117300748fc21ee97e45e05452a25aa090e038e5ec123c445e3d23|lf|lf|none|lf
tests/benchmarks/crypto/OT-157-OT005-LIBSODIUM-NOISE-XK-RADIO-EXECUTABLE-BUNDLE-PREPARATION-V0.json|6113|75e7e0998e4091d3fc4002d1f54707c4e6ac863a8fc4d7e3525e6aa6047f395f|lf|lf|none|lf
tools/ot157_noise_xk_radio_bundle.py|16815|a8139644a80141afb3c27fbdc8fc0708cf4cc96a61f54e00b02520424eb64544|lf|lf|none|lf
tools/ot157_noise_xk_radio_coordinator.py|6523|d2f912e71c801a333ec7a219b6b9345b4827287d97f9d74f1517d162ceb110b0|lf|lf|none|lf
tools/ot157_noise_xk_radio_hardware_adapter.py|3935|cb39fab624439ca7055beab56ede5b1dc1abe802a7af47123e11e75dd8553a05|lf|lf|none|lf
tests/host/ot157_noise_xk_radio_bundle_tests.py|15510|1becbb62e1cd96c79c212aeedad0f6742c310e5adc218f430c9e4c8fa704f57a|lf|lf|none|lf
tests/host/ot157_noise_xk_radio_coordinator_tests.py|15024|bd5addfe7223243128cb2065690fa07433c336660623aec6dc994d8fb049d32c|lf|lf|none|lf
tests/host/ot157_noise_xk_radio_hardware_adapter_tests.py|8338|22cc3f6c1ea1605ee84e25874718582c7375cee1dc4857b3d370320339d3bc0a|lf|lf|none|lf
""".strip()


def _load_registry() -> tuple[Entry, ...]:
    entries = []
    for line in REGISTRY.splitlines():
        path, length, sha256, policy, eol, bom, final_newline = line.split("|")
        entries.append(Entry(path, int(length), sha256, policy, eol, bom, final_newline))
    return tuple(sorted(entries, key=lambda entry: entry.path))


ENTRIES = _load_registry()


def _bom(raw: bytes) -> str:
    if raw.startswith(b"\xef\xbb\xbf"):
        return "utf8"
    if raw.startswith(b"\xff\xfe"):
        return "utf16le"
    if raw.startswith(b"\xfe\xff"):
        return "utf16be"
    return "none"


def _eol(raw: bytes) -> tuple[str, dict[str, int]]:
    crlf = raw.count(b"\r\n")
    bare_lf = raw.count(b"\n") - crlf
    bare_cr = raw.count(b"\r") - crlf
    counts = {"crlf": crlf, "bare_lf": bare_lf, "bare_cr": bare_cr}
    present = sum(value > 0 for value in counts.values())
    if present == 0:
        return "none", counts
    if present > 1:
        return "mixed", counts
    if crlf:
        return "crlf", counts
    if bare_lf:
        return "lf", counts
    return "cr", counts


def _final_newline(raw: bytes) -> str:
    if not raw:
        return "empty"
    if raw.endswith(b"\r\n"):
        return "crlf"
    if raw.endswith(b"\n"):
        return "lf"
    if raw.endswith(b"\r"):
        return "cr"
    return "none"


def _attributes(entries: tuple[Entry, ...]) -> tuple[dict[str, dict[str, str]], str | None]:
    command = ["git", "check-attr", "-z", "text", "eol", "--", *(entry.path for entry in entries)]
    try:
        completed = subprocess.run(command, cwd=ROOT, capture_output=True, check=False)
    except OSError as exc:
        return {}, f"git check-attr unavailable:{type(exc).__name__}"
    if completed.returncode != 0:
        return {}, f"git check-attr exit:{completed.returncode}"
    try:
        parts = completed.stdout.decode("utf-8").split("\0")
        attributes: dict[str, dict[str, str]] = {}
        for index in range(0, len(parts) - 1, 3):
            path, attribute, value = parts[index : index + 3]
            attributes.setdefault(path.replace("\\", "/"), {})[attribute] = value
        return attributes, None
    except (UnicodeError, ValueError) as exc:
        return {}, f"git check-attr malformed:{type(exc).__name__}"


def _registry_problems() -> list[str]:
    paths = [entry.path for entry in ENTRIES]
    problems = []
    if paths != sorted(paths):
        problems.append("paths_not_sorted")
    if len(paths) != len(set(paths)):
        problems.append("duplicate_path")
    for entry in ENTRIES:
        if entry.policy not in {"lf", "crlf", "exact"}:
            problems.append(f"invalid_policy:{entry.path}")
        if len(entry.sha256) != 64 or any(character not in "0123456789abcdef" for character in entry.sha256):
            problems.append(f"invalid_sha256:{entry.path}")
    return problems


def _print_diagnostic(payload: dict[str, object]) -> None:
    print(
        f"{SCHEMA} " + json.dumps(payload, sort_keys=True, separators=(",", ":")),
        file=sys.stderr,
    )


def main() -> int:
    registry_problems = _registry_problems()
    attributes, attribute_error = _attributes(ENTRIES)
    failures = 0

    if registry_problems or attribute_error:
        failures += 1
        _print_diagnostic(
            {
                "path": None,
                "exists": None,
                "length_actual": None,
                "length_expected": None,
                "sha256_actual": None,
                "sha256_expected": None,
                "eol_actual": None,
                "eol_expected": None,
                "bom_actual": None,
                "bom_expected": None,
                "final_newline_actual": None,
                "final_newline_expected": None,
                "attributes_actual": None,
                "attributes_expected": None,
                "problems": registry_problems + ([attribute_error] if attribute_error else []),
            }
        )

    for entry in ENTRIES:
        path = ROOT.joinpath(*entry.path.split("/"))
        exists = path.is_file()
        raw = None
        read_error = None
        if exists:
            try:
                raw = path.read_bytes()
            except OSError as exc:
                read_error = type(exc).__name__

        actual_length = len(raw) if raw is not None else None
        actual_sha256 = hashlib.sha256(raw).hexdigest() if raw is not None else None
        actual_eol, eol_counts = _eol(raw) if raw is not None else (None, None)
        actual_bom = _bom(raw) if raw is not None else None
        actual_final = _final_newline(raw) if raw is not None else None
        actual_attributes = attributes.get(entry.path)
        if entry.policy == "lf":
            expected_attributes = {"text": "set", "eol": "lf"}
            attributes_match = actual_attributes == expected_attributes
        elif entry.policy == "crlf":
            expected_attributes = {"text": "set", "eol": "crlf"}
            attributes_match = actual_attributes == expected_attributes
        else:
            expected_attributes = {"text": "unset"}
            attributes_match = bool(actual_attributes) and actual_attributes.get("text") == "unset"

        problems = []
        if not exists:
            problems.append("missing")
        if read_error:
            problems.append(f"read_error:{read_error}")
        if actual_length != entry.length:
            problems.append("length")
        if actual_sha256 != entry.sha256:
            problems.append("sha256")
        if actual_eol != entry.eol:
            problems.append("eol")
        if actual_bom != entry.bom:
            problems.append("bom")
        if actual_final != entry.final_newline:
            problems.append("final_newline")
        if not attributes_match:
            problems.append("attributes")

        if problems:
            failures += 1
            _print_diagnostic(
                {
                    "path": entry.path,
                    "exists": exists,
                    "length_actual": actual_length,
                    "length_expected": entry.length,
                    "sha256_actual": actual_sha256,
                    "sha256_expected": entry.sha256,
                    "eol_actual": actual_eol,
                    "eol_expected": entry.eol,
                    "eol_counts_actual": eol_counts,
                    "bom_actual": actual_bom,
                    "bom_expected": entry.bom,
                    "final_newline_actual": actual_final,
                    "final_newline_expected": entry.final_newline,
                    "attributes_actual": actual_attributes,
                    "attributes_expected": expected_attributes,
                    "problems": problems,
                }
            )

    if failures:
        print(
            f"{SCHEMA} checkout audit failed: {failures} diagnostic record(s) across {len(ENTRIES)} inputs",
            file=sys.stderr,
        )
        return 1
    print(f"PASS: {len(ENTRIES)} authoritative raw-byte checkout inputs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
