import abilityTest from './Ability.test';
import smokeTest from './Smoke.test';

export default function testsuite() {
  abilityTest();
  smokeTest();
}
