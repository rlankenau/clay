Strict



Import main



Global implementedTemplates:List< String > = New List< String >( [ "go", "clear", "noise", "automaton", "smooth", "expand", "contract", "darken", "lighten", "invert", "fill", "canvas", "view" ] )



Function ExecuteBox:Void( box:Box, in:Box[] )
	Select box.kind
	Case "go"
		Clear()
		
		For Local root:Box = EachIn PROJ.patch.boxes
			If root.ins = 0 And root.kind <> "go"
				root.Execute()
			EndIf
		Next
	Case "clear"
		Clear()
	Case "noise"
		Noise( box )
	Case "automata"
		Automata9Sum8( box, in[0], UnpackRules( box.settings.Get( "rules" ).value ) )
	Case "conway"
	Case "smooth"
		Smooth( box, in[0] )
	Case "expand"
		Expand( box, in[0] )
	Case "contract"
		Contract( box, in[0] )
	Case "darken"
		Darken( box, in[0], in[1] )
	Case "lighten"
		Lighten( box, in[0], in[1] )
	Case "invert"
		Invert( box, in[0] )
	Case "fill"
		Fill( box, box.settings.Get( "color" ).value )
	Case "view"
		Copy( box, in[0] )
	End
End



Function Clear:Void()
	PROJ.patch.sparks.Clear()
		
	For Local _box:Box = EachIn PROJ.patch.boxes
		Fill( _box, 0 )
		_box.done = False
	Next
End



Function Fill:Void( box:Box, value:Int )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
			box.state[x][y] = value
	Next
	Next
End



Function Noise:Void( box:Box )
	Local density:Int = box.settings.Get( "density" ).value * 5 + 5
	
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		box.state[x][y] = ( Rnd( 0, 101 ) < density )
	Next
	Next
End



Function Automata4:Void( out:Box, in:Box, rules:Int[] )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		Local total:Int
		Local dxs:Int[] = [ 0, 1, 0, -1 ]
		Local dys:Int[] = [ -1, 0, 1, 0 ]
		
		For Local d:Int = 0 To 3
			Local dx:Int = dxs[d]
			Local dy:Int = dys[d]
			
			If ( Not ( dx = 0 And dy = 0 ) ) And ( x + dx < 20 ) And ( x + dx >= 0) And ( y + dy < 15 ) And ( y + dy >= 0 )
				total = total + in.state[ x + dx ][ y + dy ]
			EndIf
		Next
		
		out.state[x][y] = rules[ total ]
	Next
	Next
End



Function Automata5Sum4:Void( out:Box, in:Box, rules:Int[] )
	'TODO
End



Function Automata5Sum5:Void( out:Box, in:Box, rules:Int[] )
	'TODO
End



Function Automata8:Void( out:Box, in:Box, rules:Int[] )
	'TODO
End



Function Automata9Sum8:Void( out:Box, in:Box, rules:Int[] )
	Local iterations:Int = out.settings.Get( "laps" ).value
	Local edge:Int = out.settings.Get( "edge" ).value
	Local a:Int[][] = Initialize2dArray( 20, 15 )
	
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		a[x][y] = in.state[x][y]
	Next
	Next
	
	While iterations >= 1
		For Local x:Int = 0 Until 20
		For Local y:Int = 0 Until 15
			Local total:Int
			
			For Local dx:Int = -1 To 1
			For Local dy:Int = -1 To 1
				If ( Not ( dx = 0 And dy = 0 ) )
					Local value:Int
					
					If ( x + dx < 20 ) And ( x + dx >= 0) And ( y + dy < 15 ) And ( y + dy >= 0 )
						value = a[ x + dx ][ y + dy ]
					Else
						Select edge
							Case 0
								value = 0
							Case 1
								value = 1
							Case 2
								Local xx:Int = ( x + dx + 20 ) Mod 20
								Local yy:Int = ( y + dy + 15) Mod 15
								Print "xx: " + xx + " yy: " + yy
								value = a[ xx ][ yy ]
							Default
						End
					EndIf
					
					total = total + value
				EndIf
			Next
			Next
			
			out.state[ x ][ y ] = rules[ total + a[ x ][ y ] * 9 ]
		Next
		Next
		
		iterations = iterations - 1
		
		If iterations >= 1
			For Local x:Int = 0 Until 20
			For Local y:Int = 0 Until 15
				a[ x ][ y ] = out.state[ x ][ y ]
			Next
			Next
		EndIf
	Wend
End



Function Automata9Sum9:Void( out:Box, in:Box, rules:Int[] )
	'TODO
End



Function Expand:Void( out:Box, in:Box )
	Automata4( out, in, [ 0, 1, 1, 1, 1 ] )
End



Function Contract:Void( out:Box, in:Box )
	Automata4( out, in, [ 0, 0, 0, 0, 1 ] )
End



Function Smooth:Void( out:Box, in:Box )
	Automata9Sum8( out, in, [ 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1 ] )
End



Function Darken:Void( out:Box, lo:Box, hi:Box )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		If hi.state[x][y] = 1
			out.state[x][y] = lo.state[x][y]
		Else
			out.state[x][y] = 0
		EndIf
	Next
	Next
End



Function Lighten:Void( out:Box, lo:Box, hi:Box )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		If hi.state[x][y] = 0
			out.state[x][y] = lo.state[x][y]
		Else
			out.state[x][y] = 1
		EndIf
	Next
	Next
End



Function Invert:Void( out:Box, in:Box )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		out.state[x][y] = 1 - in.state[x][y]
	Next
	Next
End



Function Copy:Void( a:Box, b:Box )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		a.state[x][y] = b.state[x][y]
	Next
	Next
End