// Local event storage. Each attacker owns an independent FIFO of five hits.
const DAMAGE_EVENTS_PER_PLAYER = 5;
const DAMAGE_DIGITS = 10;
const Float: DAMAGE_LIFETIME = 1.1;
const Float: DAMAGE_DIGIT_SPACING = 13.05;
// First digit center in the supplied model's two sequences, in model Y.
const Float: DAMAGE_MODEL_CENTER = -20.70;

new gl_iDamageCount[ MAX_PLAYERS + 1 ];
new gl_iDamageOrder[ MAX_PLAYERS + 1 ][ DAMAGE_EVENTS_PER_PLAYER ];
new gl_iDamageDigits[ MAX_PLAYERS + 1 ][ DAMAGE_EVENTS_PER_PLAYER ];
new gl_iDamageEntities[ MAX_PLAYERS + 1 ][ DAMAGE_EVENTS_PER_PLAYER ][ DAMAGE_DIGITS ];
new gl_iDamageToken[ MAX_PLAYERS + 1 ][ DAMAGE_EVENTS_PER_PLAYER ];
new gl_iNextDamageToken;
new Float: gl_flDamageCreated[ MAX_PLAYERS + 1 ][ DAMAGE_EVENTS_PER_PLAYER ];
new Float: gl_vecDamageOrigin[ MAX_PLAYERS + 1 ][ DAMAGE_EVENTS_PER_PLAYER ][ 3 ];

stock CDamager__DamageToInt( Float: damage )
{
	// Pawn Float cannot represent INT_MAX exactly. Check before floatround.
	if ( damage >= 2147483648.0 ) return 2147483647;
	return floatround( damage );
}
