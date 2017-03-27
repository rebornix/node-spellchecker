const SpellChecker = require('../lib/spellchecker');

SpellChecker.getCorrectionsForMisspellingAsync("helloo", (err, result) => {
	console.log(err);
	console.log(result);
});

SpellChecker.checkSpellingAsync("helloo", (err, result) => {
	console.log(err);
	console.log(result);
});

