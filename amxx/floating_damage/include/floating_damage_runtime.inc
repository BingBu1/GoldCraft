public CDamager__SpawnEntity( const pPlayer, const pVictim, Vector3( vecOrigin ), Float: flDamage, const bool: bHeadShot )
{
	if ( !IsUserValid( pPlayer ) || !is_user_connected( pPlayer ) || !( flDamage > 0.0 ) )
		return NULLENT;

	// Remove expired or externally removed events before using a FIFO slot.
	for ( new order = gl_iDamageCount[ pPlayer ] - 1; order >= 0; order-- )
	{
		new slot = gl_iDamageOrder[ pPlayer ][ order ];
		if ( get_gametime( ) - gl_flDamageCreated[ pPlayer ][ slot ] >= DAMAGE_LIFETIME || !CDamager__ValidEvent( pPlayer, slot ) )
			CDamager__RemoveEvent( pPlayer, slot );
	}
	if ( gl_iDamageCount[ pPlayer ] == DAMAGE_EVENTS_PER_PLAYER )
		CDamager__RemoveEvent( pPlayer, gl_iDamageOrder[ pPlayer ][ 0 ] );

	new slot;
	while ( gl_iDamageDigits[ pPlayer ][ slot ] ) slot++;
	new digits[ DAMAGE_DIGITS + 1 ];
	new count = num_to_str( CDamager__DamageToInt( flDamage ), digits, charsmax( digits ) );
	if ( ++gl_iNextDamageToken <= 0 ) gl_iNextDamageToken = 1;
	gl_iDamageToken[ pPlayer ][ slot ] = gl_iNextDamageToken;
	gl_flDamageCreated[ pPlayer ][ slot ] = get_gametime( );
	if ( pVictim != NULLENT )
		CDamager__GetPosition( pPlayer, pVictim, vecOrigin, vecOrigin );
	else
		vecOrigin[ 2 ] += 16.0;
	// Separate simultaneous hits spatially, without changing older events.
	vecOrigin[ 0 ] += random_float( -8.0, 8.0 );
	vecOrigin[ 1 ] += random_float( -8.0, 8.0 );
	vecOrigin[ 2 ] += float( slot ) * 5.0;
	xs_vec_copy( vecOrigin, gl_vecDamageOrigin[ pPlayer ][ slot ] );
	gl_iDamageOrder[ pPlayer ][ gl_iDamageCount[ pPlayer ]++ ] = slot;
	new skin = gl_iUserDamagerSkin[ pPlayer ] * 2 + ( bHeadShot ? 1 : 0 );
	for ( new i; i < count; i++ )
	{
		new entity = rg_create_entity( EntityDamagerReference );
		if ( is_nullent( entity ) )
		{
			CDamager__RemoveEvent( pPlayer, slot );
			return NULLENT;
		}
		gl_iDamageEntities[ pPlayer ][ slot ][ i ] = entity;
		gl_iDamageDigits[ pPlayer ][ slot ]++;
		set_entvar( entity, var_classname, EntityDamagerClassName );
		set_entvar( entity, var_owner, pPlayer );
		set_entvar( entity, var_iuser1, slot + 1 );
		set_entvar( entity, var_iuser2, gl_iDamageToken[ pPlayer ][ slot ] );
		set_entvar( entity, var_iuser3, i );
		set_entvar( entity, var_solid, SOLID_NOT );
		set_entvar( entity, var_movetype, MOVETYPE_NONE );
		set_entvar( entity, var_effects, EF_OWNER_VISIBILITY );
		set_entvar( entity, var_skin, skin );
		// bodygroup 0 only: 0 is blank, 1..10 are the digits 0..9.
		// Every other bodygroup stays blank, even with an 8-bit delta field.
		set_entvar( entity, var_body, digits[ i ] - '0' + 1 );
		engfunc( EngFunc_SetModel, entity, EntityDamagerModel );
		UTIL_SetEntityAnim( entity, 0, .flFrameRate = 0.0 );
		UTIL_SetEntityRendering( entity, _, _, kRenderTransAdd, 255.0 );
	}
	new anchor = gl_iDamageEntities[ pPlayer ][ slot ][ 0 ];
	// Each glyph retains an expiry callback if another plugin removes the anchor.
	for ( new i; i < count; i++ )
	{
		new entity = gl_iDamageEntities[ pPlayer ][ slot ][ i ];
		SetThink( entity, "CDamager__Think" );
		set_entvar( entity, var_nextthink, gl_flDamageCreated[ pPlayer ][ slot ] + DAMAGE_LIFETIME );
	}
	CDamager__Think( anchor );
	return anchor;
}

stock bool: CDamager__ValidDigit( const player, const slot, const digit )
{
	new entity = gl_iDamageEntities[ player ][ slot ][ digit ];
	return !is_nullent( entity ) && FClassnameIs( entity, EntityDamagerClassName )
		&& get_entvar( entity, var_owner ) == player
		&& get_entvar( entity, var_iuser1 ) == slot + 1
		&& get_entvar( entity, var_iuser2 ) == gl_iDamageToken[ player ][ slot ]
		&& get_entvar( entity, var_iuser3 ) == digit;
}

stock bool: CDamager__ValidEvent( const player, const slot )
{
	for ( new i; i < gl_iDamageDigits[ player ][ slot ]; i++ )
		if ( !CDamager__ValidDigit( player, slot, i ) ) return false;
	return gl_iDamageDigits[ player ][ slot ] > 0;
}

stock CDamager__RemoveEvent( const player, const slot )
{
	for ( new i; i < gl_iDamageDigits[ player ][ slot ]; i++ )
	{
		if ( CDamager__ValidDigit( player, slot, i ) )
			engfunc( EngFunc_RemoveEntity, gl_iDamageEntities[ player ][ slot ][ i ] );
		gl_iDamageEntities[ player ][ slot ][ i ] = 0;
	}
	gl_iDamageDigits[ player ][ slot ] = 0;
	for ( new order; order < gl_iDamageCount[ player ]; order++ )
	{
		if ( gl_iDamageOrder[ player ][ order ] != slot ) continue;
		for ( new next = order + 1; next < gl_iDamageCount[ player ]; next++ )
			gl_iDamageOrder[ player ][ next - 1 ] = gl_iDamageOrder[ player ][ next ];
		gl_iDamageCount[ player ]--;
		break;
	}
}

stock CDamager__ClearPlayer( const player )
{
	while ( gl_iDamageCount[ player ] )
		CDamager__RemoveEvent( player, gl_iDamageOrder[ player ][ 0 ] );
}

public CDamager__ClearAll( )
{
	for ( new player = 1; player <= MaxClients; player++ ) CDamager__ClearPlayer( player );
}

public CDamager__Think( const pEntity )
{
	if ( is_nullent( pEntity ) ) return;
	new owner = get_entvar( pEntity, var_owner );
	new slot = get_entvar( pEntity, var_iuser1 ) - 1;
	if ( !IsUserValid( owner ) || slot < 0 || slot >= DAMAGE_EVENTS_PER_PLAYER ) return;
	new digit = get_entvar( pEntity, var_iuser3 );
	if ( digit < 0 || digit >= gl_iDamageDigits[ owner ][ slot ] || pEntity != gl_iDamageEntities[ owner ][ slot ][ digit ] || !CDamager__ValidDigit( owner, slot, digit ) ) return;
	new Float: age = get_gametime( ) - gl_flDamageCreated[ owner ][ slot ];
	if ( !is_user_connected( owner ) || age >= DAMAGE_LIFETIME || !CDamager__ValidEvent( owner, slot ) )
	{
		CDamager__RemoveEvent( owner, slot );
		return;
	}
	if ( digit != 0 )
	{
		set_entvar( pEntity, var_nextthink, gl_flDamageCreated[ owner ][ slot ] + DAMAGE_LIFETIME );
		return;
	}
	new Vector3( angles ); get_entvar( owner, var_v_angle, angles );
	angles[ 1 ] -= 180.0;
	new Vector3( right ); angle_vector( angles, ANGLEVECTOR_RIGHT, right );
	for ( new i; i < gl_iDamageDigits[ owner ][ slot ]; i++ )
	{
		new entity = gl_iDamageEntities[ owner ][ slot ][ i ];
		new Vector3( origin ); xs_vec_copy( gl_vecDamageOrigin[ owner ][ slot ], origin );
		origin[ 2 ] += age * 64.0;
		new Float: shift = float( i ) * DAMAGE_DIGIT_SPACING - float( gl_iDamageDigits[ owner ][ slot ] - 1 ) * DAMAGE_DIGIT_SPACING * 0.5 - DAMAGE_MODEL_CENTER;
		xs_vec_add_scaled( origin, right, -shift, origin );
		engfunc( EngFunc_SetOrigin, entity, origin );
		set_entvar( entity, var_angles, angles );
		if ( age >= 0.3 )
		{
			if ( get_entvar( entity, var_sequence ) != 1 ) UTIL_SetEntityAnim( entity, 1, .flFrameRate = EntityDamagerAnimFrameRate );
			set_entvar( entity, var_renderamt, floatmax( 0.0, 255.0 - ( age - 0.3 ) * 300.0 ) );
		}
	}
	set_entvar( pEntity, var_nextthink, get_gametime( ) + EntityDamagerNextThink );
}
