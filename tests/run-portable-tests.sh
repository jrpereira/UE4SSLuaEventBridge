#!/usr/bin/env bash
set -euo pipefail

c++ -std=c++20 -Wall -Wextra -Werror -Ibridge/include -Icontract tests/DispatchBudgetTests.cpp -o /tmp/ue4ss-dispatch-budget-tests
/tmp/ue4ss-dispatch-budget-tests

c++ -std=c++20 -Wall -Wextra -Werror -pthread \
    -Ibridge/include -Icontract \
    tests/SessionAliasIndexTests.cpp \
    -o /tmp/ue4ss-lua-event-bridge-tests

/tmp/ue4ss-lua-event-bridge-tests

c++ -std=c++20 -Wall -Wextra -Werror -Ibridge/include -Icontract tests/BindingSnapshotTests.cpp -o /tmp/ue4ss-binding-snapshot-tests
/tmp/ue4ss-binding-snapshot-tests

c++ -std=c++20 -Wall -Wextra -Werror -Ibridge/include -Icontract tests/LegacyInstallMigrationTests.cpp -o /tmp/ue4ss-legacy-install-migration-tests
/tmp/ue4ss-legacy-install-migration-tests

c++ -std=c++20 -Wall -Wextra -Werror -Ibridge/include -Icontract tests/WeakObjectPtrTests.cpp -o /tmp/ue4ss-weak-object-tests
/tmp/ue4ss-weak-object-tests

c++ -std=c++20 -Wall -Wextra -Werror -pthread -Ibridge/include -Icontract tests/QueueBuffersTests.cpp -o /tmp/ue4ss-queue-buffer-tests
/tmp/ue4ss-queue-buffer-tests

c++ -std=c++20 -Wall -Wextra -Werror -Ibridge/include -Icontract tests/QueueDispatchScheduleTests.cpp -o /tmp/ue4ss-queue-schedule-tests
/tmp/ue4ss-queue-schedule-tests

c++ -std=c++20 -Wall -Wextra -Werror -pthread -Ibridge/include -Icontract tests/LifecycleRegistryTests.cpp -o /tmp/ue4ss-lifecycle-registry-tests
/tmp/ue4ss-lifecycle-registry-tests

c++ -std=c++20 -Wall -Wextra -Werror -Ibootstrap/include bootstrap/tests/BootstrapSelectionTests.cpp -o /tmp/ue4ss-bootstrap-selection-tests
/tmp/ue4ss-bootstrap-selection-tests
